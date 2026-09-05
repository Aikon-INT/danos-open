/*
 * DANOS-Open Performance Baseline (H1-H2)
 *
 * Measures:
 *   H1: DPA transaction throughput (commits/sec)
 *   H2: Object operation latency (create/read/update/delete)
 *
 * These are micro-benchmarks for the DPA core engine, not dataplane
 * forwarding performance (which requires VPP/DPDK).
 */

#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* H1: Transaction throughput */
static int bench_tx_throughput(void)
{
    const int N = 10000;
    uint64_t start = now_ns();

    for (int i = 0; i < N; i++) {
        danos_tx_t tx = {0};
        if (danos_tx_begin(&tx, "bench", NULL) != DANOS_OK) return -1;
        danos_tx_prepare(&tx);
        danos_tx_validate(&tx);
        danos_tx_commit(&tx);
    }

    uint64_t elapsed = now_ns() - start;
    double secs = (double)elapsed / 1e9;
    double tps = (double)N / secs;

    printf("[H1] Transaction throughput:\n");
    printf("     %d transactions in %.3f s\n", N, secs);
    printf("     %.0f tx/sec\n", tps);
    printf("     %.3f us/tx\n", 1e6 / tps);

    /* Baseline: should achieve at least 1000 tx/sec */
    if (tps < 1000) {
        printf("     [WARN] below baseline (1000 tx/sec)\n");
        return 1;
    }
    printf("     [PASS] meets baseline\n");
    return 0;
}

/* H2: Object CRUD latency */
static int bench_object_crud(void)
{
    const int N = 5000;
    danos_tx_t tx = {0};

    /* Create */
    uint64_t start = now_ns();
    danos_tx_begin(&tx, "bench-create", NULL);
    for (int i = 0; i < N; i++) {
        danos_iface_t iface;
        memset(&iface, 0, sizeof(iface));
        iface.ifindex = (danos_ifindex_t)(i + 1);
        snprintf(iface.name, sizeof(iface.name), "eth%d", i);
        iface.mtu = 1500;
        iface.admin_up = true;
        danos_iface_create(&tx, &iface);
    }
    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);
    uint64_t create_ns = now_ns() - start;

    /* Read */
    start = now_ns();
    danos_tx_begin(&tx, "bench-read", NULL);
    for (int i = 0; i < N; i++) {
        danos_iface_t iface;
        danos_iface_read(&tx, (danos_ifindex_t)(i + 1), &iface);
    }
    danos_tx_commit(&tx);
    uint64_t read_ns = now_ns() - start;

    /* Delete */
    start = now_ns();
    danos_tx_begin(&tx, "bench-delete", NULL);
    for (int i = 0; i < N; i++) {
        danos_iface_delete(&tx, (danos_ifindex_t)(i + 1));
    }
    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);
    uint64_t delete_ns = now_ns() - start;

    printf("[H2] Object CRUD latency (%d objects):\n", N);
    printf("     create: %.3f us/op  (%.0f ops/sec)\n",
           (double)create_ns / N / 1000, (double)N * 1e9 / create_ns);
    printf("     read:   %.3f us/op  (%.0f ops/sec)\n",
           (double)read_ns / N / 1000, (double)N * 1e9 / read_ns);
    printf("     delete: %.3f us/op  (%.0f ops/sec)\n",
           (double)delete_ns / N / 1000, (double)N * 1e9 / delete_ns);

    /* Baseline: each op should be < 100 us */
    double create_us = (double)create_ns / N / 1000;
    double read_us = (double)read_ns / N / 1000;
    double delete_us = (double)delete_ns / N / 1000;

    int ok = 0;
    if (create_us > 100) { printf("     [WARN] create above baseline (100 us)\n"); ok++; }
    if (read_us > 100)   { printf("     [WARN] read above baseline (100 us)\n"); ok++; }
    if (delete_us > 100) { printf("     [WARN] delete above baseline (100 us)\n"); ok++; }
    if (ok == 0) printf("     [PASS] meets baseline\n");
    return ok;
}

/* H1b: Concurrent transaction throughput (multi-reader) */
static int bench_concurrent_read(void)
{
    const int N = 5000;
    danos_tx_t tx = {0};

    /* Setup: create some objects */
    danos_tx_begin(&tx, "setup", NULL);
    for (int i = 0; i < 100; i++) {
        danos_iface_t iface;
        memset(&iface, 0, sizeof(iface));
        iface.ifindex = (danos_ifindex_t)(i + 1);
        snprintf(iface.name, sizeof(iface.name), "eth%d", i);
        iface.mtu = 1500;
        iface.admin_up = true;
        danos_iface_create(&tx, &iface);
    }
    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);

    /* Concurrent reads (simulated: multiple read txs in sequence) */
    uint64_t start = now_ns();
    for (int i = 0; i < N; i++) {
        danos_tx_t rtx = {0};
        danos_tx_begin(&rtx, "read", NULL);
        danos_iface_t iface;
        danos_iface_read(&rtx, (danos_ifindex_t)((i % 100) + 1), &iface);
        danos_tx_commit(&rtx);
    }
    uint64_t elapsed = now_ns() - start;
    double secs = (double)elapsed / 1e9;
    double tps = (double)N / secs;

    printf("[H1b] Concurrent read throughput:\n");
    printf("      %d reads in %.3f s (%.0f reads/sec)\n", N, secs, tps);

    if (tps < 1000) {
        printf("      [WARN] below baseline (1000 reads/sec)\n");
        return 1;
    }
    printf("      [PASS] meets baseline\n");
    return 0;
}

int main(void)
{
    printf("=== DANOS-Open Performance Baseline ===\n\n");

    int warnings = 0;
    warnings += bench_tx_throughput();
    printf("\n");
    warnings += bench_object_crud();
    printf("\n");
    warnings += bench_concurrent_read();

    printf("\n=== Baseline complete: %d warnings ===\n", warnings);
    /* Warnings are not failures — they indicate baseline targets not met */
    return 0;
}
