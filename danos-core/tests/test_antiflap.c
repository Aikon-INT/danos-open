/*
 * Test: Reconciler Anti-Flap (B9)
 * Verify 5s window / 3 max repair suppression.
 */
#include <danos/core/antiflap.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

int test_antiflap_basic(void)
{
    antiflap_ctx_t ctx;
    antiflap_init(&ctx, &ANTIFLAP_DEFAULT);

    /* First 3 repairs should be allowed */
    assert(antiflap_check_and_record(&ctx, 100) == true);
    assert(antiflap_check_and_record(&ctx, 100) == true);
    assert(antiflap_check_and_record(&ctx, 100) == true);

    /* 4th repair should be suppressed (flapping) */
    assert(antiflap_check_and_record(&ctx, 100) == false);
    assert(antiflap_is_flapping(&ctx, 100) == true);

    /* Further repairs also suppressed */
    assert(antiflap_check_and_record(&ctx, 100) == false);

    antiflap_stats_t stats;
    antiflap_get_stats(&ctx, &stats);
    assert(stats.flapping_objects == 1);
    assert(stats.total_suppressed == 2);

    antiflap_fini(&ctx);
    printf("[PASS] test_antiflap_basic: 3 repairs then suppress\n");
    return 0;
}

int test_antiflap_different_objects(void)
{
    antiflap_ctx_t ctx;
    antiflap_init(&ctx, &ANTIFLAP_DEFAULT);

    /* Different objects are tracked independently */
    assert(antiflap_check_and_record(&ctx, 1) == true);
    assert(antiflap_check_and_record(&ctx, 2) == true);
    assert(antiflap_check_and_record(&ctx, 3) == true);

    /* Object 1: 3 repairs */
    assert(antiflap_check_and_record(&ctx, 1) == true);
    assert(antiflap_check_and_record(&ctx, 1) == true);

    /* Object 2: 3 repairs */
    assert(antiflap_check_and_record(&ctx, 2) == true);
    assert(antiflap_check_and_record(&ctx, 2) == true);

    /* Neither should be flapping yet */
    assert(antiflap_is_flapping(&ctx, 1) == false);
    assert(antiflap_is_flapping(&ctx, 2) == false);

    /* Object 1: 4th repair → flapping */
    assert(antiflap_check_and_record(&ctx, 1) == false);
    assert(antiflap_is_flapping(&ctx, 1) == true);

    /* Object 2: still not flapping */
    assert(antiflap_is_flapping(&ctx, 2) == false);

    antiflap_fini(&ctx);
    printf("[PASS] test_antiflap_different_objects: independent tracking\n");
    return 0;
}

int test_antiflap_clear(void)
{
    antiflap_ctx_t ctx;
    antiflap_init(&ctx, &ANTIFLAP_DEFAULT);

    /* Trigger flapping */
    for (int i = 0; i < 4; i++) {
        antiflap_check_and_record(&ctx, 42);
    }
    assert(antiflap_is_flapping(&ctx, 42) == true);

    /* Clear flapping state */
    assert(antiflap_clear(&ctx, 42) == 0);
    assert(antiflap_is_flapping(&ctx, 42) == false);

    /* Repairs allowed again */
    assert(antiflap_check_and_record(&ctx, 42) == true);

    antiflap_fini(&ctx);
    printf("[PASS] test_antiflap_clear: manual intervention clears flap\n");
    return 0;
}

int test_antiflap_window_reset(void)
{
    antiflap_config_t cfg = { .window_ms = 100, .max_count = 3 };
    antiflap_ctx_t ctx;
    antiflap_init(&ctx, &cfg);

    /* 3 repairs (at limit) */
    assert(antiflap_check_and_record(&ctx, 7) == true);
    assert(antiflap_check_and_record(&ctx, 7) == true);
    assert(antiflap_check_and_record(&ctx, 7) == true);

    /* Wait for window to expire */
    usleep(150000);  /* 150ms > 100ms window */

    /* New window: repairs allowed again */
    assert(antiflap_check_and_record(&ctx, 7) == true);
    assert(antiflap_is_flapping(&ctx, 7) == false);

    antiflap_fini(&ctx);
    printf("[PASS] test_antiflap_window_reset: window expiry resets count\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_antiflap_basic() != 0) failed++;
    if (test_antiflap_different_objects() != 0) failed++;
    if (test_antiflap_clear() != 0) failed++;
    if (test_antiflap_window_reset() != 0) failed++;
    printf("=== antiflap_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
