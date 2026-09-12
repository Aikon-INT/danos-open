/*
 * Test: Configuration persistence (v0.3)
 *
 * Simulates a full restart cycle in one process:
 *   boot 1: enable persistence, create objects, mutate, delete one
 *   "crash": disable persistence (WAL on disk), drop the store
 *   boot 2: recreate store, recover(), verify state matches
 * plus torn-record tolerance and a reconciler run after recovery.
 */

#include <danos/core/persist.h>
#include <danos/core/wal.h>
#include <danos/core/object_registry.h>
#include <danos/core/reconciler.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#define WAL_PATH "/tmp/danos-test-persist.wal"

static void fresh_boot(void)
{
    /* emulate process start: no store, WAL on disk */
    g_default_store = NULL;
    unlink(WAL_PATH);
}

int test_enable_and_log(void)
{
    fresh_boot();
    assert(danos_persist_enable(WAL_PATH) == 0);
    assert(danos_persist_is_enabled());

    /* create one interface via the DPA CRUD path */
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "persist-test", NULL) == DANOS_OK);
    danos_iface_t ifc;
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifindex = 7;
    strcpy(ifc.name, "wan0");
    ifc.mtu = 9000;
    ifc.admin_up = true;
    assert(danos_iface_create(&tx, &ifc) == DANOS_OK);
    assert(danos_tx_prepare(&tx) == DANOS_OK);
    assert(danos_tx_validate(&tx) == DANOS_OK);
    assert(danos_tx_commit(&tx) == DANOS_OK);

    /* update it */
    assert(danos_tx_begin(&tx, "persist-test", NULL) == DANOS_OK);
    ifc.mtu = 1500;
    assert(danos_iface_update(&tx, &ifc) == DANOS_OK);
    assert(danos_tx_prepare(&tx) == DANOS_OK);
    assert(danos_tx_validate(&tx) == DANOS_OK);
    assert(danos_tx_commit(&tx) == DANOS_OK);

    /* a second object then deleted */
    danos_iface_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.ifindex = 8;
    strcpy(tmp.name, "tmp0");
    tmp.mtu = 1500;
    assert(danos_tx_begin(&tx, "persist-test", NULL) == DANOS_OK);
    assert(danos_iface_create(&tx, &tmp) == DANOS_OK);
    assert(danos_tx_prepare(&tx) == DANOS_OK);
    assert(danos_tx_validate(&tx) == DANOS_OK);
    assert(danos_tx_commit(&tx) == DANOS_OK);
    assert(danos_tx_begin(&tx, "persist-test", NULL) == DANOS_OK);
    assert(danos_iface_delete(&tx, 8) == DANOS_OK);
    assert(danos_tx_prepare(&tx) == DANOS_OK);
    assert(danos_tx_validate(&tx) == DANOS_OK);
    assert(danos_tx_commit(&tx) == DANOS_OK);

    uint64_t logged = 0, recovered = 0;
    danos_persist_get_stats(&logged, &recovered);
    assert(logged == 4);   /* create + update + create + delete */

    printf("[PASS] test_enable_and_log\n");
    return 0;
}

int test_recover_after_restart(void)
{
    /* "crash": persistence off, store dropped */
    danos_persist_disable();
    g_default_store = NULL;

    /* boot 2: re-enable (same WAL), recover */
    assert(danos_persist_enable(WAL_PATH) == 0);
    uint64_t logged_before = 0, rec = 0;
    danos_persist_get_stats(&logged_before, &rec);
    int applied = danos_persist_recover();
    assert(applied >= 2);   /* wan0 create+update applied */

    /* wan0 survived with the LAST mutation (mtu 1500) */
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "verify", NULL) == DANOS_OK);
    danos_iface_t out;
    assert(danos_iface_read(&tx, 7, &out) == DANOS_OK);
    assert(strcmp(out.name, "wan0") == 0);
    assert(out.mtu == 1500);

    /* tmp0 (deleted) must not come back */
    assert(danos_iface_read(&tx, 8, &out) == DANOS_ERR_NOT_FOUND);
    assert(danos_tx_abort(&tx) == DANOS_OK);

    /* store counts: exactly one iface */
    assert(danos_object_count(g_default_store, DANOS_OBJ_IFACE) == 1);

    /* recovery must not have re-logged into the WAL (no feedback loop) */
    uint64_t logged = 0, recovered = 0;
    danos_persist_get_stats(&logged, &recovered);
    assert(logged == logged_before);
    assert(recovered >= 2);

    printf("[PASS] test_recover_after_restart\n");
    return 0;
}

int test_reconcile_after_recovery(void)
{
    /* Recovered objects live in the default store; feed them into the
     * state store as DESIRED and run one reconcile pass. Nothing is
     * PROGRAMMED, so the pass must report every object as a diff. */
    danos_state_store_t *ss = danos_state_store_create();
    assert(ss);

    /* copy recovered default-store objects into desired */
    {
        /* iterate via public API: use count + read by probing ids we know */
        danos_tx_t tx;
        assert(danos_tx_begin(&tx, "recon", NULL) == DANOS_OK);
        danos_iface_t out;
        assert(danos_iface_read(&tx, 7, &out) == DANOS_OK);
        assert(danos_tx_abort(&tx) == DANOS_OK);
        assert(danos_state_set(ss, DANOS_STATE_DESIRED, DANOS_OBJ_IFACE, 7,
                               &out, sizeof(out)) == DANOS_OK);
    }

    danos_reconcile_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    assert(danos_reconciler_init(ss, &cfg) == 0);
    uint64_t diffs = danos_reconciler_run_once();
    assert(diffs >= 1);   /* wan0 desired, nothing programmed */
    danos_reconciler_fini();
    danos_state_store_destroy(ss);

    printf("[PASS] test_reconcile_after_recovery (diffs=%lu)\n",
           (unsigned long)diffs);
    return 0;
}

int test_torn_record_tolerance(void)
{
    danos_persist_disable();
    g_default_store = NULL;

    /* append garbage to the WAL (simulated torn write) */
    FILE *f = fopen(WAL_PATH, "ab");
    assert(f);
    const char junk[] = "HALF-WRITTEN RECORD";
    fwrite(junk, 1, sizeof(junk) - 1, f);
    fclose(f);

    assert(danos_persist_enable(WAL_PATH) == 0);
    int applied = danos_persist_recover();
    /* valid records still recovered; junk discarded */
    assert(applied >= 2);

    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "verify2", NULL) == DANOS_OK);
    danos_iface_t out;
    assert(danos_iface_read(&tx, 7, &out) == DANOS_OK);
    assert(danos_tx_abort(&tx) == DANOS_OK);

    danos_persist_disable();
    unlink(WAL_PATH);
    printf("[PASS] test_torn_record_tolerance\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_enable_and_log() != 0) failed++;
    if (test_recover_after_restart() != 0) failed++;
    if (test_reconcile_after_recovery() != 0) failed++;
    if (test_torn_record_tolerance() != 0) failed++;
    printf("=== persist_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
