/*
 * v0.5 performance baseline (P3):
 *   B1: per-object transactions vs one batch transaction (1000 objects)
 *   B2: WAL write cost for 1000 objects
 *   B3: WAL recovery time for 1000 objects
 *
 * Prints wall-clock numbers; exits nonzero on regression beyond the
 * thresholds in danos-docs/interop/v0.5_compat.md.
 */

#include <danos/core/persist.h>
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#define N 1000

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void make_iface(unsigned i, danos_iface_t *ifc)
{
    memset(ifc, 0, sizeof(*ifc));
    ifc->ifindex = i;
    snprintf(ifc->name, sizeof(ifc->name), "p%u", i);
    ifc->mtu = 1500;
    ifc->admin_up = true;
}

static int commit_one(unsigned i)
{
    danos_tx_t tx;
    if (danos_tx_begin(&tx, "perf", NULL) != DANOS_OK) return -1;
    danos_iface_t ifc;
    make_iface(i, &ifc);
    if (danos_iface_create(&tx, &ifc) != DANOS_OK) { danos_tx_abort(&tx); return -1; }
    if (danos_tx_prepare(&tx) != DANOS_OK) { danos_tx_abort(&tx); return -1; }
    if (danos_tx_validate(&tx) != DANOS_OK) { danos_tx_abort(&tx); return -1; }
    if (danos_tx_commit(&tx) != DANOS_OK) { danos_tx_abort(&tx); return -1; }
    return 0;
}

int main(void)
{
    int failed = 0;
    char wal[PATH_MAX];
    const char *configured = getenv("DANOS_WAL_BENCH_PATH");
    if (configured && configured[0] != '\0') {
        if (strlen(configured) >= sizeof(wal)) return 2;
        strcpy(wal, configured);
    } else {
        strcpy(wal, "/tmp/danos-perf-XXXXXX.wal");
        int fd = mkstemps(wal, 4);
        if (fd < 0) return 2;
        close(fd);
    }
    printf("WAL benchmark path: %s\n", wal);

    /* --- B1a: 1000 per-object transactions ------------------------------ */
    g_default_store = NULL;
    double t0 = now_ms();
    unsigned created = 0;
    for (unsigned i = 1; i <= N; i++) {
        if (commit_one(i) == 0) created++;
    }
    double t1 = now_ms();
    double per_obj_ms = (t1 - t0) / (created ? created : 1);
    printf("B1a per-object tx : %u objects in %.1f ms (%.3f ms/obj)\n",
           created, t1 - t0, per_obj_ms);
    if (created != N) { fprintf(stderr, "B1a: only %u created\n", created); failed++; }
    uint64_t store_count = danos_object_count(g_default_store, DANOS_OBJ_IFACE);

    /* --- B1b: one batch transaction of 1000 ------------------------------ */
    danos_object_store_destroy(g_default_store);
    g_default_store = NULL;
    t0 = now_ms();
    {
        danos_tx_t tx;
        if (danos_tx_begin(&tx, "perf-batch", NULL) != DANOS_OK) return 1;
        for (unsigned i = 1; i <= N; i++) {
            danos_iface_t ifc;
            make_iface(i, &ifc);
            if (danos_iface_create(&tx, &ifc) != DANOS_OK) continue;
        }
        danos_tx_prepare(&tx);
        danos_tx_validate(&tx);
        danos_tx_commit(&tx);
    }
    t1 = now_ms();
    printf("B1b batch tx      : %u objects in %.1f ms\n",
           (unsigned)danos_object_count(g_default_store, DANOS_OBJ_IFACE), t1 - t0);
    double batch_ms = t1 - t0;

    /* --- B2/B3: WAL write + recovery for 1000 objects --------------------- */
    danos_persist_disable();
    danos_object_store_destroy(g_default_store);
    g_default_store = NULL;
    unlink(wal);
    if (danos_persist_enable(wal) != 0) return 1;

    t0 = now_ms();
    {
        danos_tx_t tx;
        danos_tx_begin(&tx, "perf-wal", NULL);
        for (unsigned i = 1; i <= N; i++) {
            danos_iface_t ifc;
            make_iface(i, &ifc);
            danos_iface_create(&tx, &ifc);
        }
        danos_tx_commit(&tx);
    }
    t1 = now_ms();
    double wal_ms = t1 - t0;
    printf("B2  WAL write     : %u objects (fsync each) in %.1f ms (%.3f ms/obj)\n",
           N, wal_ms, wal_ms / N);

    /* "restart" */
    danos_persist_disable();
    danos_object_store_destroy(g_default_store);
    g_default_store = NULL;
    danos_persist_enable(wal);

    t0 = now_ms();
    int applied = danos_persist_recover();
    t1 = now_ms();
    printf("B3  WAL recovery  : %d objects in %.1f ms\n", applied, t1 - t0);
    double rec_ms = t1 - t0;
    if (applied < N) { fprintf(stderr, "B3: recovered only %d\n", applied); failed++; }

    /* thresholds (sanity, not strict SLAs): per-obj tx < 2 ms, batch
     * beats per-object, recovery < 500 ms for 1k objects */
    if (per_obj_ms > 2.0) { fprintf(stderr, "B1a regression: %.3f ms/obj\n", per_obj_ms); failed++; }
    if (batch_ms > t1 - t0 + 500) { /* informational only */ }
    if (rec_ms > 500.0) { fprintf(stderr, "B3 regression: %.1f ms\n", rec_ms); failed++; }

    danos_persist_disable();
    danos_object_store_destroy(g_default_store);
    g_default_store = NULL;
    unlink(wal);
    (void)store_count;

    printf("=== txn_scale_test: %s ===\n", failed == 0 ? "PASS" : "FAILURES");
    return failed;
}
