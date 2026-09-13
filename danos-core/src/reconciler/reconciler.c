/*
 * DANOS-Open Core: Reconciler implementation (B8)
 */

#include <danos/core/backend_ops.h>
#include <danos/core/persist.h>
#include <danos/core/reconciler.h>
#include <danos/core/event_bus.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

danos_reconciler_t *g_reconciler = NULL;

static const danos_reconcile_config_t kDefaultConfig = {
    .reconcile_period_ms = 30000,
    .max_retries         = 5,
    .backoff_initial_ms  = 1000,
    .backoff_max_ms      = 60000,
    .antiflap_window_ms  = 5000,
    .antiflap_max_count  = 3,
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int danos_reconciler_init(danos_state_store_t *state,
                          const danos_reconcile_config_t *config)
{
    if (g_reconciler) return 0;
    g_reconciler = calloc(1, sizeof(*g_reconciler));
    if (!g_reconciler) return -1;
    g_reconciler->config = config ? *config : kDefaultConfig;
    g_reconciler->state = state;
    g_reconciler->running = false;
    pthread_mutex_init(&g_reconciler->stats_lock, NULL);
    memset(&g_reconciler->stats, 0, sizeof(g_reconciler->stats));
    return 0;
}

void danos_reconciler_fini(void)
{
    if (!g_reconciler) return;
    danos_reconciler_stop();
    pthread_mutex_destroy(&g_reconciler->stats_lock);
    free(g_reconciler);
    g_reconciler = NULL;
}

/* Diff callback: count diffs and emit events */
static int diff_cb(danos_obj_type_t type, danos_obj_id_t id, void *user)
{
    /* v0.9: diff callback only records; actual (re)programming runs in
     * danos_programming_run() which walks desired state and drives the
     * backend ops. Kept as a hook for type-specific fast paths. */
    (void)type; (void)id; (void)user;
    return 0;
}

uint64_t danos_reconciler_run_once(void)
{
    if (!g_reconciler) return 0;
    if (!g_reconciler->state) {
        /* programming-only mode: no state store, drive the backend */
        uint64_t attempted = 0, failed = 0;
        uint64_t ok = danos_programming_run(&attempted, &failed);
        pthread_mutex_lock(&g_reconciler->stats_lock);
        g_reconciler->stats.total_repairs += attempted;
        g_reconciler->stats.total_failures += failed;
        pthread_mutex_unlock(&g_reconciler->stats_lock);
        return ok;
    }

    pthread_mutex_lock(&g_reconciler->stats_lock);
    g_reconciler->stats.total_runs++;
    pthread_mutex_unlock(&g_reconciler->stats_lock);

    uint64_t diffs = danos_state_diff_desired_programmed(
        g_reconciler->state, diff_cb, NULL);

    /* v0.9: real programming pass against the installed backend ops */
    uint64_t attempted = 0, failed = 0;
    uint64_t programmed = danos_programming_run(&attempted, &failed);

    pthread_mutex_lock(&g_reconciler->stats_lock);
    g_reconciler->stats.total_diffs += diffs;
    g_reconciler->stats.total_repairs += attempted;   /* real attempts */
    g_reconciler->stats.total_failures += failed;     /* honest accounting */
    pthread_mutex_unlock(&g_reconciler->stats_lock);

    (void)programmed;

    /* Emit reconcile event */
    if (diffs > 0 && g_event_bus) {
        danos_event_t ev = {0};
        ev.type = DANOS_EVENT_RECONCILE;
        ev.timestamp_ns = now_ns();
        ev.message = "reconcile repaired drift";
        danos_event_publish(&ev);
    }
    return diffs;
}

/* Periodic reconcile thread — also wakes immediately when desired
 * state is marked dirty by a store mutation event (v0.13). */
static void *reconcile_thread(void *arg)
{
    (void)arg;
    uint64_t period_us = g_reconciler->config.reconcile_period_ms * 1000;
    uint64_t waited = 0;
    while (g_reconciler->running) {
        usleep(50000);   /* 50 ms tick */
        if (!g_reconciler->running) break;
        waited += 50000;
        if (!danos_programming_dirty_take() && waited < period_us)
            continue;
        waited = 0;
        danos_reconciler_run_once();
    }
    return NULL;
}

int danos_reconciler_start(void)
{
    if (!g_reconciler) return -1;
    if (g_reconciler->running) return 0;
    g_reconciler->running = true;
    if (pthread_create(&g_reconciler->thread, NULL, reconcile_thread, NULL) != 0) {
        g_reconciler->running = false;
        return -1;
    }
    return 0;
}

void danos_reconciler_stop(void)
{
    if (!g_reconciler || !g_reconciler->running) return;
    g_reconciler->running = false;
    pthread_join(g_reconciler->thread, NULL);
}

/* Public API */
danos_status_t danos_reconcile_trigger(danos_obj_type_t type)
{
    (void)type;
    uint64_t diffs = danos_reconciler_run_once();
    return diffs > 0 ? DANOS_OK : DANOS_OK;
}

danos_status_t danos_reconcile_get_stats(danos_obj_type_t type,
                                         danos_reconcile_stats_t *out)
{
    (void)type;
    if (!out) return DANOS_ERR_INVALID_ARG;
    if (!g_reconciler) { memset(out, 0, sizeof(*out)); return DANOS_OK; }
    pthread_mutex_lock(&g_reconciler->stats_lock);
    *out = g_reconciler->stats;
    pthread_mutex_unlock(&g_reconciler->stats_lock);
    return DANOS_OK;
}

void danos_reconciler_global_stats(uint64_t *runs, uint64_t *diffs,
                                   uint64_t *repairs, uint64_t *failures)
{
    if (!g_reconciler) return;
    pthread_mutex_lock(&g_reconciler->stats_lock);
    if (runs)     *runs     = g_reconciler->stats.total_runs;
    if (diffs)    *diffs    = g_reconciler->stats.total_diffs;
    if (repairs)  *repairs  = g_reconciler->stats.total_repairs;
    if (failures) *failures = g_reconciler->stats.total_failures;
    pthread_mutex_unlock(&g_reconciler->stats_lock);
}
