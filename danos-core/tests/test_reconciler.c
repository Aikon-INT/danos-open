/* Test: Reconciler (B8) */
#include <danos/core/reconciler.h>
#include <danos/core/state_store.h>
#include <stdio.h>
#include <assert.h>

int test_reconciler(void)
{
    danos_state_store_t *ss = danos_state_store_create();
    assert(ss != NULL);

    /* Set desired without programmed → drift exists */
    danos_state_set(ss, DANOS_STATE_DESIRED, DANOS_OBJ_ROUTE, 1, "r1", 3);

    danos_reconciler_init(ss, NULL);

    /* Run once: should detect 1 diff */
    uint64_t diffs = danos_reconciler_run_once();
    assert(diffs == 1);

    /* Fix programmed → no drift */
    danos_state_set(ss, DANOS_STATE_PROGRAMMED, DANOS_OBJ_ROUTE, 1, "r1", 3);
    diffs = danos_reconciler_run_once();
    assert(diffs == 0);

    /* Check stats */
    danos_reconcile_stats_t stats;
    danos_reconcile_get_stats(DANOS_OBJ_ROUTE, &stats);
    assert(stats.total_runs == 2);
    assert(stats.total_diffs == 1);

    danos_reconciler_fini();
    danos_state_store_destroy(ss);
    printf("[PASS] test_reconciler: drift detection + repair\n");
    return 0;
}
