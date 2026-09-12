/*
 * DANOS-Open Observability: Unit Tests
 */

#include <danos/observability/prometheus.h>
#include <danos/observability/log.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_pass = 0;
static int test_stat_binding(void);

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_pass++; } \
    else { printf("  FAIL: %s\n", msg); } \
} while(0)

static void test_prometheus(void)
{
    printf("== Prometheus ==\n");
    danos_prom_init();

    /* Default metrics should be registered (40+) */
    char buf[16384];
    int n = danos_prom_render(buf, sizeof(buf));
    CHECK(n > 0, "render produces output");
    CHECK(strstr(buf, "danos_tx_total") != NULL, "tx_total metric present");
    CHECK(strstr(buf, "danos_bgp_peers") != NULL, "bgp_peers metric present");
    CHECK(strstr(buf, "# HELP") != NULL, "HELP lines present");
    CHECK(strstr(buf, "# TYPE") != NULL, "TYPE lines present");

    /* Set and increment */
    CHECK(danos_prom_set("danos_bgp_peers", 5) == 0, "set bgp_peers=5");
    CHECK(danos_prom_inc("danos_tx_total", 1) == 0, "inc tx_total");

    n = danos_prom_render(buf, sizeof(buf));
    CHECK(strstr(buf, "danos_bgp_peers 5.000000") != NULL, "bgp_peers value=5");

    /* Register custom metric */
    CHECK(danos_prom_register("danos_custom", "custom metric", DANOS_METRIC_GAUGE) == 0,
          "register custom");
    CHECK(danos_prom_register("danos_custom", "duplicate", DANOS_METRIC_GAUGE) == -1,
          "duplicate register fails");

    /* Histogram */
    CHECK(danos_prom_observe("danos_tx_duration_seconds", 0.05) == 0, "observe histogram");
    n = danos_prom_render(buf, sizeof(buf));
    CHECK(strstr(buf, "danos_tx_duration_seconds_count") != NULL, "histogram count");
}

static void test_log(void)
{
    printf("== Structured Log ==\n");
    danos_log_init("test", DANOS_LOG_INFO);

    /* These should produce JSON output to stderr */
    danos_log(DANOS_LOG_INFO, "test message", "key1", "val1", NULL);
    danos_log(DANOS_LOG_WARN, "warning message", NULL);
    danos_log(DANOS_LOG_ERROR, "error message", "code", "500", NULL);

    /* DEBUG should be filtered (min_level = INFO) */
    danos_log(DANOS_LOG_DEBUG, "should not appear", NULL);

    /* Change level */
    danos_log_set_level(DANOS_LOG_DEBUG);
    danos_log(DANOS_LOG_DEBUG, "now visible", NULL);

    CHECK(1, "log functions executed without crash");
}

static int test_stat_binding(void);

int main(void)
{
    printf("=== DANOS Observability Test Suite ===\n\n");
    test_prometheus();
    test_log();
    tests_run++;
    if (test_stat_binding() == 0) tests_pass++;
    printf("\n=== Result: %d passed, %d failed, %d total ===\n",
           tests_pass, tests_run - tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}

/* ---- v0.6: stat provider binding ------------------------------------------ */
#include <string.h>
#include <stdint.h>
#include <assert.h>

static uint64_t fake_provider(const char *name)
{
    if (strcmp(name, "/sys/node/vectors") == 0) return 4242;
    return 0;
}

static int test_stat_binding(void)
{
    if (danos_prom_register("test_bound_total", "t", DANOS_METRIC_COUNTER) != 0)
        return -1;
    danos_prom_set_stat_provider(fake_provider);
    assert(danos_prom_bind_stat("test_bound_total", "/sys/node/vectors") == 0);
    assert(danos_prom_refresh() == 1);

    char buf[8192];
    danos_prom_render(buf, sizeof(buf));
    assert(strstr(buf, "test_bound_total 4242.000000"));
    printf("[PASS] test_stat_binding: provider refresh at render\n");
    return 0;
}
