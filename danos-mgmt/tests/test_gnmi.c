/*
 * Test: gNMI-like REST/JSON server (E2)
 */
#include "../src/gnmi/gnmi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

int test_gnmi_get(void)
{
    /* GET /gnmi/interfaces */
    char *resp = danos_gnmi_handle_request("GET", "/gnmi/interfaces", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "interfaces") != NULL);
    free(resp);

    /* GET /gnmi/routes */
    resp = danos_gnmi_handle_request("GET", "/gnmi/routes", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "routes") != NULL);
    free(resp);

    /* GET /gnmi/vrfs */
    resp = danos_gnmi_handle_request("GET", "/gnmi/vrfs", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "default") != NULL);
    free(resp);

    /* GET /gnmi/capabilities */
    resp = danos_gnmi_handle_request("GET", "/gnmi/capabilities", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "dpa_version") != NULL);
    free(resp);

    printf("[PASS] test_gnmi_get: GET endpoints work\n");
    return 0;
}

int test_gnmi_set_interface(void)
{
    /* SET /gnmi/interfaces with valid body */
    const char *body = "{\"ifindex\": 1, \"name\": \"eth0\", \"mtu\": 1500}";
    char *resp = danos_gnmi_handle_request("SET", "/gnmi/interfaces", body);
    assert(resp != NULL);
    assert(strstr(resp, "created") != NULL);
    assert(strstr(resp, "eth0") != NULL);
    free(resp);

    /* Empty body */
    resp = danos_gnmi_handle_request("SET", "/gnmi/interfaces", "");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* Missing ifindex */
    resp = danos_gnmi_handle_request("SET", "/gnmi/interfaces", "{\"name\": \"eth0\"}");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    printf("[PASS] test_gnmi_set_interface: SET interface works\n");
    return 0;
}

int test_gnmi_set_route(void)
{
    /* SET /gnmi/routes with valid body */
    const char *body = "{\"vrf_id\": 0, \"prefix_len\": 24, \"nhgroup_id\": 1}";
    char *resp = danos_gnmi_handle_request("SET", "/gnmi/routes", body);
    assert(resp != NULL);
    assert(strstr(resp, "created") != NULL || strstr(resp, "error") != NULL);
    free(resp);

    /* Missing nhgroup_id */
    resp = danos_gnmi_handle_request("SET", "/gnmi/routes", "{\"vrf_id\": 0}");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    printf("[PASS] test_gnmi_set_route: SET route works\n");
    return 0;
}

int test_gnmi_not_found(void)
{
    char *resp = danos_gnmi_handle_request("GET", "/gnmi/unknown", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "not found") != NULL);
    free(resp);

    resp = danos_gnmi_handle_request("DELETE", "/gnmi/interfaces", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "method not allowed") != NULL);
    free(resp);

    printf("[PASS] test_gnmi_not_found: 404 and 405 handling\n");
    return 0;
}

int test_gnmi_server_lifecycle(void)
{
    danos_gnmi_ctx_t ctx;
    danos_gnmi_init(&ctx, 18080);
    assert(ctx.listen_fd == -1);
    assert(ctx.running == false);

    int rc = danos_gnmi_start(&ctx);
    assert(rc == 0);
    assert(ctx.running == true);
    assert(ctx.listen_fd >= 0);

    danos_gnmi_stop(&ctx);
    assert(ctx.running == false);
    assert(ctx.listen_fd == -1);

    printf("[PASS] test_gnmi_server_lifecycle: start/stop works\n");
    return 0;
}

/* v0.2: gNMI Subscribe */
int test_gnmi_subscribe(void)
{
    danos_gnmi_subscribe_init();
    assert(danos_gnmi_subscribe_count() == 0);

    /* Subscribe to all object-created events */
    uint64_t sub1 = danos_gnmi_subscribe(DANOS_OBJ_INVALID,
                                          DANOS_EVENT_OBJ_CREATED);
    assert(sub1 > 0);
    assert(danos_gnmi_subscribe_count() == 1);

    /* Subscribe to interface updates only */
    uint64_t sub2 = danos_gnmi_subscribe(DANOS_OBJ_IFACE,
                                          DANOS_EVENT_OBJ_UPDATED);
    assert(sub2 > 0);
    assert(danos_gnmi_subscribe_count() == 2);

    /* Invalid mask → returns 0 */
    uint64_t sub3 = danos_gnmi_subscribe(DANOS_OBJ_IFACE, 0);
    assert(sub3 == 0);
    assert(danos_gnmi_subscribe_count() == 2);

    /* Poll empty queue → "[]" */
    char *resp = danos_gnmi_subscribe_poll(sub1);
    assert(resp != NULL);
    assert(strcmp(resp, "[]") == 0);
    free(resp);

    /* Poll invalid subscription */
    resp = danos_gnmi_subscribe_poll(99999);
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* Unsubscribe */
    danos_gnmi_unsubscribe(sub1);
    assert(danos_gnmi_subscribe_count() == 1);
    danos_gnmi_unsubscribe(sub2);
    assert(danos_gnmi_subscribe_count() == 0);

    danos_gnmi_subscribe_fini();
    printf("[PASS] test_gnmi_subscribe: subscribe/unsubscribe/poll\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_gnmi_get() != 0) failed++;
    if (test_gnmi_set_interface() != 0) failed++;
    if (test_gnmi_set_route() != 0) failed++;
    if (test_gnmi_not_found() != 0) failed++;
    if (test_gnmi_server_lifecycle() != 0) failed++;
    if (test_gnmi_subscribe() != 0) failed++;
    printf("=== gnmi_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
