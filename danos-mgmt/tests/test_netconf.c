/*
 * Test: NETCONF Server (E3)
 */
#include "../src/netconf/netconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int test_netconf_parse(void)
{
    assert(netconf_parse_rpc("<rpc><get-config/></rpc>") == NETCONF_RPC_GET_CONFIG);
    assert(netconf_parse_rpc("<rpc><edit-config/></rpc>") == NETCONF_RPC_EDIT_CONFIG);
    assert(netconf_parse_rpc("<rpc><commit/></rpc>") == NETCONF_RPC_COMMIT);
    assert(netconf_parse_rpc("<rpc><discard-changes/></rpc>") == NETCONF_RPC_DISCARD);
    assert(netconf_parse_rpc("<rpc><close-session/></rpc>") == NETCONF_RPC_CLOSE_SESSION);
    assert(netconf_parse_rpc("<rpc><lock/></rpc>") == NETCONF_RPC_LOCK);
    assert(netconf_parse_rpc("<rpc><unknown-op/></rpc>") == NETCONF_RPC_UNKNOWN);
    assert(netconf_parse_rpc(NULL) == NETCONF_RPC_UNKNOWN);

    printf("[PASS] test_netconf_parse: RPC type detection\n");
    return 0;
}

int test_netconf_hello(void)
{
    char *hello = netconf_hello_message();
    assert(hello != NULL);
    assert(strstr(hello, "hello") != NULL);
    assert(strstr(hello, "capabilities") != NULL);
    assert(strstr(hello, "netconf:base:1.0") != NULL);
    assert(strstr(hello, "netconf:base:1.1") != NULL);
    assert(strstr(hello, "danos:yang") != NULL);
    free(hello);

    printf("[PASS] test_netconf_hello: hello message has capabilities\n");
    return 0;
}

int test_netconf_handle_rpc(void)
{
    netconf_ctx_t ctx;
    netconf_init(&ctx, 830);

    /* get-config */
    char *resp = netconf_handle_rpc(&ctx, "<rpc><get-config><source><running/></source></get-config></rpc>");
    assert(resp != NULL);
    assert(strstr(resp, "data") != NULL);
    free(resp);

    /* edit-config */
    resp = netconf_handle_rpc(&ctx, "<rpc><edit-config><target><running/></target></edit-config></rpc>");
    assert(resp != NULL);
    assert(strstr(resp, "ok") != NULL);
    free(resp);

    /* commit */
    resp = netconf_handle_rpc(&ctx, "<rpc><commit/></rpc>");
    assert(resp != NULL);
    assert(strstr(resp, "ok") != NULL);
    free(resp);

    /* unknown */
    resp = netconf_handle_rpc(&ctx, "<rpc><foobar/></rpc>");
    assert(resp != NULL);
    assert(strstr(resp, "rpc-error") != NULL);
    free(resp);

    /* Stats */
    uint64_t rpc_count, error_count;
    netconf_get_stats(&ctx, &rpc_count, &error_count);
    assert(rpc_count == 4);
    assert(error_count == 1);

    printf("[PASS] test_netconf_handle_rpc: RPC dispatch works\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_netconf_parse() != 0) failed++;
    if (test_netconf_hello() != 0) failed++;
    if (test_netconf_handle_rpc() != 0) failed++;
    printf("=== netconf_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
