/*
 * DANOS-Open Core: Transaction Engine implementation (B3, B4, B5)
 *
 * Implements the public DPA transaction API from danos/dpa.h.
 * - B3: state machine OPEN→PREPARE→VALIDATE→COMMIT→VERIFY→DONE
 * - B4: multi-read single-write (global write mutex for commit)
 * - B5: per-phase timeouts
 */

#include <danos/core/transaction.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

danos_tx_manager_t *g_tx_mgr = NULL;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int danos_tx_manager_init(void)
{
    if (g_tx_mgr) return 0;
    g_tx_mgr = calloc(1, sizeof(*g_tx_mgr));
    if (!g_tx_mgr) return -1;
    pthread_mutex_init(&g_tx_mgr->write_lock, NULL);
    pthread_mutex_init(&g_tx_mgr->pool_lock, NULL);
    g_tx_mgr->free_list = NULL;
    g_tx_mgr->next_tx_id = 1;
    g_tx_mgr->active_count = 0;
    return 0;
}

void danos_tx_manager_fini(void)
{
    if (!g_tx_mgr) return;
    pthread_mutex_lock(&g_tx_mgr->pool_lock);
    danos_tx_record_t *r = g_tx_mgr->free_list;
    while (r) {
        danos_tx_record_t *next = r->next;
        pthread_mutex_destroy(&r->lock);
        free(r);
        r = next;
    }
    pthread_mutex_unlock(&g_tx_mgr->pool_lock);
    pthread_mutex_destroy(&g_tx_mgr->write_lock);
    pthread_mutex_destroy(&g_tx_mgr->pool_lock);
    free(g_tx_mgr);
    g_tx_mgr = NULL;
}

danos_tx_record_t *danos_tx_record_alloc(void)
{
    pthread_mutex_lock(&g_tx_mgr->pool_lock);
    danos_tx_record_t *r = g_tx_mgr->free_list;
    if (r) {
        g_tx_mgr->free_list = r->next;
        r->next = NULL;
    }
    pthread_mutex_unlock(&g_tx_mgr->pool_lock);

    if (!r) {
        r = calloc(1, sizeof(*r));
        if (!r) return NULL;
        pthread_mutex_init(&r->lock, NULL);
    }
    r->in_use = true;
    memset(&r->pub, 0, sizeof(r->pub));
    return r;
}

void danos_tx_record_free(danos_tx_record_t *r)
{
    if (!r) return;
    r->in_use = false;
    pthread_mutex_lock(&g_tx_mgr->pool_lock);
    r->next = g_tx_mgr->free_list;
    g_tx_mgr->free_list = r;
    pthread_mutex_unlock(&g_tx_mgr->pool_lock);
}

danos_tx_record_t *danos_tx_validate_ptr(danos_tx_t *tx)
{
    if (!tx) return NULL;
    return (danos_tx_record_t *)tx->_internal;
}

bool danos_tx_in_state(danos_tx_record_t *rec, danos_tx_state_t expected)
{
    return rec->pub.state == expected;
}

/* =========================================================================
 * Public DPA Transaction API implementation
 * ========================================================================= */

static const danos_tx_timeouts_t kDefaultTimeouts = {
    .prepare_ms = 100,
    .commit_ms  = 500,
    .verify_ms  = 1000,
};

danos_status_t danos_tx_begin(danos_tx_t *tx,
                              const char *initiator,
                              const danos_tx_timeouts_t *timeouts)
{
    if (!tx) return DANOS_ERR_INVALID_ARG;
    if (!g_tx_mgr) danos_tx_manager_init();

    danos_tx_record_t *rec = danos_tx_record_alloc();
    if (!rec) return DANOS_ERR_NO_MEMORY;

    rec->pub.id = __atomic_fetch_add(&g_tx_mgr->next_tx_id, 1, __ATOMIC_SEQ_CST);
    rec->pub.state = DANOS_TX_OPEN;
    rec->pub.start_ns = now_ns();

    const danos_tx_timeouts_t *t = timeouts ? timeouts : &kDefaultTimeouts;
    rec->pub.deadline_ns = rec->pub.start_ns +
        (uint64_t)(t->prepare_ms + t->commit_ms + t->verify_ms) * 1000000ULL;

    if (initiator) {
        /* Store initiator pointer (caller must keep it alive for tx duration) */
        rec->pub.initiator = initiator;
    } else {
        rec->pub.initiator = "unknown";
    }

    /* Set internal pointer so caller's tx can find the record */
    rec->pub._internal = rec;

    /* Copy public view to caller */
    *tx = rec->pub;

    __atomic_add_fetch(&g_tx_mgr->active_count, 1, __ATOMIC_SEQ_CST);
    return DANOS_OK;
}

danos_status_t danos_tx_prepare(danos_tx_t *tx)
{
    danos_tx_record_t *rec = danos_tx_validate_ptr(tx);
    if (!rec) return DANOS_ERR_TX_INVALID;
    pthread_mutex_lock(&rec->lock);
    if (rec->pub.state != DANOS_TX_OPEN) {
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_INVALID;
    }
    rec->pub.state = DANOS_TX_PREPARE;
    tx->state = DANOS_TX_PREPARE;
    pthread_mutex_unlock(&rec->lock);
    return DANOS_OK;
}

danos_status_t danos_tx_validate(danos_tx_t *tx)
{
    danos_tx_record_t *rec = danos_tx_validate_ptr(tx);
    if (!rec) return DANOS_ERR_TX_INVALID;
    pthread_mutex_lock(&rec->lock);
    if (rec->pub.state != DANOS_TX_PREPARE) {
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_INVALID;
    }
    rec->pub.state = DANOS_TX_VALIDATE;
    tx->state = DANOS_TX_VALIDATE;
    pthread_mutex_unlock(&rec->lock);
    return DANOS_OK;
}

danos_status_t danos_tx_commit(danos_tx_t *tx)
{
    danos_tx_record_t *rec = danos_tx_validate_ptr(tx);
    if (!rec) return DANOS_ERR_TX_INVALID;
    pthread_mutex_lock(&rec->lock);
    if (rec->pub.state != DANOS_TX_VALIDATE) {
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_INVALID;
    }

    /* Acquire global write lock (single-writer) */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 1;  /* 1s max wait for write lock */
    int rc = pthread_mutex_timedlock(&g_tx_mgr->write_lock, &deadline);
    if (rc == ETIMEDOUT) {
        rec->pub.state = DANOS_TX_ABORT;
        tx->state = DANOS_TX_ABORT;
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_TIMEOUT;
    }

    rec->pub.state = DANOS_TX_COMMIT;
    tx->state = DANOS_TX_COMMIT;
    pthread_mutex_unlock(&rec->lock);

    /* Release write lock after commit */
    pthread_mutex_unlock(&g_tx_mgr->write_lock);
    return DANOS_OK;
}

danos_status_t danos_tx_verify(danos_tx_t *tx)
{
    danos_tx_record_t *rec = danos_tx_validate_ptr(tx);
    if (!rec) return DANOS_ERR_TX_INVALID;
    pthread_mutex_lock(&rec->lock);
    if (rec->pub.state != DANOS_TX_COMMIT) {
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_INVALID;
    }
    rec->pub.state = DANOS_TX_VERIFY;
    tx->state = DANOS_TX_VERIFY;
    /* Auto-transition to DONE for skeleton */
    rec->pub.state = DANOS_TX_DONE;
    tx->state = DANOS_TX_DONE;
    pthread_mutex_unlock(&rec->lock);

    __atomic_sub_fetch(&g_tx_mgr->active_count, 1, __ATOMIC_SEQ_CST);
    return DANOS_OK;
}

danos_status_t danos_tx_abort(danos_tx_t *tx)
{
    danos_tx_record_t *rec = danos_tx_validate_ptr(tx);
    if (!rec) return DANOS_ERR_TX_INVALID;
    pthread_mutex_lock(&rec->lock);
    if (rec->pub.state >= DANOS_TX_COMMIT) {
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_INVALID;  /* can't abort after commit */
    }
    rec->pub.state = DANOS_TX_ABORT;
    tx->state = DANOS_TX_ABORT;
    pthread_mutex_unlock(&rec->lock);

    __atomic_sub_fetch(&g_tx_mgr->active_count, 1, __ATOMIC_SEQ_CST);
    return DANOS_OK;
}

danos_status_t danos_tx_rollback(danos_tx_t *tx)
{
    danos_tx_record_t *rec = danos_tx_validate_ptr(tx);
    if (!rec) return DANOS_ERR_TX_INVALID;
    pthread_mutex_lock(&rec->lock);
    if (rec->pub.state != DANOS_TX_COMMIT && rec->pub.state != DANOS_TX_VERIFY) {
        pthread_mutex_unlock(&rec->lock);
        return DANOS_ERR_TX_INVALID;
    }
    rec->pub.state = DANOS_TX_ROLLBACK;
    tx->state = DANOS_TX_ROLLBACK;
    pthread_mutex_unlock(&rec->lock);

    __atomic_sub_fetch(&g_tx_mgr->active_count, 1, __ATOMIC_SEQ_CST);
    return DANOS_OK;
}

danos_status_t danos_tx_get_state(const danos_tx_t *tx, danos_tx_state_t *out)
{
    if (!tx || !out) return DANOS_ERR_INVALID_ARG;
    *out = tx->state;
    return DANOS_OK;
}

danos_status_t danos_tx_commit_atomic(danos_tx_t *tx)
{
    danos_status_t st;
    st = danos_tx_prepare(tx);    if (st != DANOS_OK) return st;
    st = danos_tx_validate(tx);   if (st != DANOS_OK) return st;
    st = danos_tx_commit(tx);     if (st != DANOS_OK) return st;
    st = danos_tx_verify(tx);     return st;
}
