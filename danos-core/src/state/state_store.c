/*
 * DANOS-Open Core: State Store implementation (B2)
 */

#include <danos/core/state_store.h>
#include <stdlib.h>
#include <string.h>

danos_state_store_t *danos_state_store_create(void)
{
    danos_state_store_t *ss = calloc(1, sizeof(*ss));
    if (!ss) return NULL;
    ss->desired    = danos_object_store_create(1024);
    ss->programmed = danos_object_store_create(1024);
    ss->oper       = danos_object_store_create(1024);
    if (!ss->desired || !ss->programmed || !ss->oper) {
        danos_state_store_destroy(ss);
        return NULL;
    }
    return ss;
}

void danos_state_store_destroy(danos_state_store_t *ss)
{
    if (!ss) return;
    danos_object_store_destroy(ss->desired);
    danos_object_store_destroy(ss->programmed);
    danos_object_store_destroy(ss->oper);
    free(ss);
}

static danos_object_store_t *pick_store(danos_state_store_t *ss,
                                        danos_state_kind_t kind)
{
    switch (kind) {
    case DANOS_STATE_DESIRED:    return ss->desired;
    case DANOS_STATE_PROGRAMMED: return ss->programmed;
    case DANOS_STATE_OPER:       return ss->oper;
    default: return NULL;
    }
}

danos_status_t danos_state_set(danos_state_store_t *ss,
                               danos_state_kind_t kind,
                               danos_obj_type_t type, danos_obj_id_t id,
                               const void *data, size_t size)
{
    danos_object_store_t *s = pick_store(ss, kind);
    if (!s) return DANOS_ERR_INVALID_ARG;
    /* Try create first; if exists, update */
    danos_status_t st = danos_object_create(s, type, id, data, size);
    if (st == DANOS_ERR_EXISTS) st = danos_object_update(s, type, id, data, size);
    return st;
}

danos_status_t danos_state_get(danos_state_store_t *ss,
                               danos_state_kind_t kind,
                               danos_obj_type_t type, danos_obj_id_t id,
                               void *out, size_t *out_size)
{
    danos_object_store_t *s = pick_store(ss, kind);
    if (!s) return DANOS_ERR_INVALID_ARG;
    return danos_object_read(s, type, id, out, out_size);
}

danos_status_t danos_state_delete(danos_state_store_t *ss,
                                  danos_state_kind_t kind,
                                  danos_obj_type_t type, danos_obj_id_t id)
{
    danos_object_store_t *s = pick_store(ss, kind);
    if (!s) return DANOS_ERR_INVALID_ARG;
    return danos_object_delete(s, type, id);
}

/* Diff: iterate desired, check if programmed matches */
uint64_t danos_state_diff_desired_programmed(danos_state_store_t *ss,
                                             danos_state_diff_cb_t cb,
                                             void *user)
{
    if (!ss || !cb) return 0;
    uint64_t diffs = 0;

    /* Walk desired store buckets */
    pthread_rwlock_rdlock(&ss->desired->lock);
    for (size_t i = 0; i < ss->desired->bucket_count; i++) {
        danos_object_entry_t *e = ss->desired->buckets[i];
        while (e) {
            /* Check programmed: first query size, then read */
            bool differs = false;
            size_t psize = 0;
            /* Try read with zero size to get required size */
            danos_status_t st = danos_object_read(ss->programmed,
                                                  e->type, e->id,
                                                  NULL, &psize);
            if (st == DANOS_ERR_NOT_FOUND) {
                differs = true;
            } else if (st == DANOS_ERR_INVALID_ARG && psize > 0) {
                /* Object exists, need to read with proper buffer */
                void *pbuf = malloc(psize);
                if (pbuf) {
                    size_t rsize = psize;
                    st = danos_object_read(ss->programmed, e->type, e->id,
                                           pbuf, &rsize);
                    if (st == DANOS_OK && rsize == e->data_size) {
                        if (memcmp(pbuf, e->data, e->data_size) != 0)
                            differs = true;
                    } else {
                        differs = true;
                    }
                    free(pbuf);
                } else {
                    differs = true;
                }
            } else {
                differs = true;
            }
            if (differs) {
                diffs++;
                cb(e->type, e->id, user);
            }
            e = e->next;
        }
    }
    pthread_rwlock_unlock(&ss->desired->lock);
    return diffs;
}
