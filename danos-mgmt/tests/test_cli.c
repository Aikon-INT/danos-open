/*
 * Test: CLI (E1)
 * Verify CLI command parsing and dispatch.
 */
#include "../src/cli/cli.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int test_cli_basic(void)
{
    danos_cli_ctx_t ctx;
    danos_cli_init(&ctx);

    /* Empty line */
    assert(danos_cli_process(&ctx, "") == 0);
    assert(danos_cli_process(&ctx, "   ") == 0);

    /* Help */
    assert(danos_cli_process(&ctx, "help") == 0);
    assert(danos_cli_process(&ctx, "?") == 0);

    /* Show commands */
    assert(danos_cli_process(&ctx, "show interface") == 0);
    assert(danos_cli_process(&ctx, "show route") == 0);
    assert(danos_cli_process(&ctx, "show vrf") == 0);
    assert(danos_cli_process(&ctx, "show int") == 0);  /* abbreviation */

    printf("[PASS] test_cli_basic: basic commands work\n");
    return 0;
}

int test_cli_configure_mode(void)
{
    danos_cli_ctx_t ctx;
    danos_cli_init(&ctx);

    assert(ctx.in_configure_mode == false);
    assert(strcmp(ctx.prompt, "danos> ") == 0);

    /* Enter configure mode */
    assert(danos_cli_process(&ctx, "configure") == 0);
    assert(ctx.in_configure_mode == true);
    assert(strcmp(ctx.prompt, "danos(config)# ") == 0);

    /* Exit configure mode */
    assert(danos_cli_process(&ctx, "exit") == 0);
    assert(ctx.in_configure_mode == false);
    assert(strcmp(ctx.prompt, "danos> ") == 0);

    printf("[PASS] test_cli_configure_mode: configure/exit works\n");
    return 0;
}

int test_cli_exit(void)
{
    danos_cli_ctx_t ctx;
    danos_cli_init(&ctx);

    /* exit at top level returns 1 (quit) */
    assert(danos_cli_process(&ctx, "exit") == 1);
    assert(danos_cli_process(&ctx, "quit") == 1);

    printf("[PASS] test_cli_exit: exit returns 1 at top level\n");
    return 0;
}

int test_cli_unknown(void)
{
    danos_cli_ctx_t ctx;
    danos_cli_init(&ctx);

    /* Unknown command returns -1 */
    assert(danos_cli_process(&ctx, "foobar") == -1);
    assert(danos_cli_process(&ctx, "show foobar") == -1);

    printf("[PASS] test_cli_unknown: unknown commands return -1\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_cli_basic() != 0) failed++;
    if (test_cli_configure_mode() != 0) failed++;
    if (test_cli_exit() != 0) failed++;
    if (test_cli_unknown() != 0) failed++;
    printf("=== cli_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
