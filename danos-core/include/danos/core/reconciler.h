/*
 * DANOS-Open Core: Reconciler (B8)
 *
 * Compares Desired vs Programmed/Oper state and repairs drift.
 * - Event-driven: oper change events trigger immediate reconcile
 * - Periodic: full diff scan every reconcile_period_ms
 * - Backoff: exponential on failure
 * - Anti-flap: max N retries per object in window
 */

#ifndef DANOS_CORE_RECONCILER_H__
#define DANOS_CORE_RECONCILER_H__

#include <danos/core/state_store.h>
#include <danos/dpa.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    danos_reconcile_config_t config;
    danos_state_store_t     *state;
    bool                     running;
    pthread_t                thread;
    danos_reconcile_stats_t  stats;
    pthread_mutex_t          stats_lock;
} danos_reconciler_t;

extern danos_reconciler_t *g_reconciler;

int danos_reconciler_init(danos_state_store_t *state,
                          const danos_reconcile_config_t *config);
void danos_reconciler_fini(void);

/* Start periodic reconcile thread */
int danos_reconciler_start(void);
void danos_reconciler_stop(void);

/* One-shot reconcile: scan all object types */
uint64_t danos_reconciler_run_once(void);

/* Global reconciler counters (v0.13, Prometheus-visible) */
void danos_reconciler_global_stats(uint64_t *runs, uint64_t *diffs,
                                   uint64_t *repairs, uint64_t *failures);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_CORE_RECONCILER_H__ */
