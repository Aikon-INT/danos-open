/*
 * Test: gNMI-like REST/JSON server (E2)
 */
#include "../src/gnmi/gnmi.h"
#include <danos/core/event_bus.h>
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

    /* PUT method is not supported by any path → "method not allowed" */
    resp = danos_gnmi_handle_request("PUT", "/gnmi/interfaces", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "method not allowed") != NULL);
    free(resp);

    /* DELETE on path without id → not found */
    resp = danos_gnmi_handle_request("DELETE", "/gnmi/interfaces", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "not found") != NULL);
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

    /* Subscribe to all object-created events (bitmask: bit N set) */
    uint32_t mask_created = (1u << DANOS_EVENT_OBJ_CREATED);
    uint64_t sub1 = danos_gnmi_subscribe(DANOS_OBJ_INVALID, mask_created);
    assert(sub1 > 0);
    assert(danos_gnmi_subscribe_count() == 1);

    /* Subscribe to interface updates only */
    uint32_t mask_updated = (1u << DANOS_EVENT_OBJ_UPDATED);
    uint64_t sub2 = danos_gnmi_subscribe(DANOS_OBJ_IFACE, mask_updated);
    assert(sub2 > 0);
    assert(danos_gnmi_subscribe_count() == 2);

    /* Invalid mask (0) → returns 0 */
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

/* v0.2: gNMI Subscribe end-to-end with event bus publish */
int test_gnmi_subscribe_e2e(void)
{
    danos_gnmi_subscribe_init();

    /* Subscribe to all OBJ_CREATED events on any object type */
    uint32_t mask_created = (1u << DANOS_EVENT_OBJ_CREATED);
    uint64_t sub = danos_gnmi_subscribe(DANOS_OBJ_INVALID, mask_created);
    assert(sub > 0);

    /* Publish an OBJ_CREATED event via the event bus */
    danos_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DANOS_EVENT_OBJ_CREATED;
    ev.obj_type = DANOS_OBJ_IFACE;
    ev.obj_id = 42;
    ev.timestamp_ns = 12345;
    danos_event_publish(&ev);

    /* Poll: should receive one notification */
    char *resp = danos_gnmi_subscribe_poll(sub);
    assert(resp != NULL);
    /* Should contain "created" and "interface" and "42" */
    assert(strstr(resp, "created") != NULL);
    assert(strstr(resp, "interface") != NULL);
    assert(strstr(resp, "42") != NULL);
    free(resp);

    /* Second poll: queue empty → "[]" */
    resp = danos_gnmi_subscribe_poll(sub);
    assert(resp != NULL);
    assert(strcmp(resp, "[]") == 0);
    free(resp);

    /* Publish an OBJ_UPDATED event: should NOT match (mask only CREATED) */
    ev.type = DANOS_EVENT_OBJ_UPDATED;
    danos_event_publish(&ev);
    resp = danos_gnmi_subscribe_poll(sub);
    assert(resp != NULL);
    assert(strcmp(resp, "[]") == 0);
    free(resp);

    /* Publish another CREATED on a different object type: should match */
    ev.type = DANOS_EVENT_OBJ_CREATED;
    ev.obj_type = DANOS_OBJ_ROUTE;
    ev.obj_id = 99;
    danos_event_publish(&ev);
    resp = danos_gnmi_subscribe_poll(sub);
    assert(resp != NULL);
    assert(strstr(resp, "created") != NULL);
    assert(strstr(resp, "route") != NULL);
    assert(strstr(resp, "99") != NULL);
    free(resp);

    danos_gnmi_unsubscribe(sub);
    danos_gnmi_subscribe_fini();
    printf("[PASS] test_gnmi_subscribe_e2e: event publish → notification\n");
    return 0;
}

/* v0.2: gNMI ACL paths */
int test_gnmi_acl(void)
{
    /* GET /gnmi/acl/tables */
    char *resp = danos_gnmi_handle_request("GET", "/gnmi/acl/tables", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "acl/tables") != NULL);
    free(resp);

    /* SET /gnmi/acl/tables with valid body */
    const char *body = "{\"table_id\": 1, \"name\": \"acl1\", "
                        "\"bind_ifindex\": 2, \"ingress\": true}";
    resp = danos_gnmi_handle_request("SET", "/gnmi/acl/tables", body);
    assert(resp != NULL);
    assert(strstr(resp, "ok") != NULL || strstr(resp, "error") != NULL);
    free(resp);

    /* Empty body */
    resp = danos_gnmi_handle_request("SET", "/gnmi/acl/tables", "");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* Missing table_id */
    resp = danos_gnmi_handle_request("SET", "/gnmi/acl/tables",
                                     "{\"name\": \"acl1\"}");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* DELETE /gnmi/acl/tables/1 */
    resp = danos_gnmi_handle_request("DELETE", "/gnmi/acl/tables/1", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "deleted") != NULL || strstr(resp, "error") != NULL);
    free(resp);

    /* DELETE with invalid id */
    resp = danos_gnmi_handle_request("DELETE", "/gnmi/acl/tables/0", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    printf("[PASS] test_gnmi_acl: ACL GET/SET/DELETE paths\n");
    return 0;
}

/* v0.2: gNMI QoS paths */
int test_gnmi_qos(void)
{
    /* GET /gnmi/qos/policies */
    char *resp = danos_gnmi_handle_request("GET", "/gnmi/qos/policies", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "qos/policies") != NULL);
    free(resp);

    /* SET /gnmi/qos/policies with valid body */
    const char *body = "{\"policy_id\": 1, \"name\": \"pol1\", "
                        "\"cir_bps\": 1000000, \"cb_bytes\": 8000}";
    resp = danos_gnmi_handle_request("SET", "/gnmi/qos/policies", body);
    assert(resp != NULL);
    assert(strstr(resp, "ok") != NULL || strstr(resp, "error") != NULL);
    free(resp);

    /* Empty body */
    resp = danos_gnmi_handle_request("SET", "/gnmi/qos/policies", "");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* Missing policy_id */
    resp = danos_gnmi_handle_request("SET", "/gnmi/qos/policies",
                                     "{\"name\": \"pol1\"}");
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* DELETE /gnmi/qos/policies/1 */
    resp = danos_gnmi_handle_request("DELETE", "/gnmi/qos/policies/1", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "deleted") != NULL || strstr(resp, "error") != NULL);
    free(resp);

    printf("[PASS] test_gnmi_qos: QoS GET/SET/DELETE paths\n");
    return 0;
}

/* v0.2: gNMI Subscribe via HTTP dispatch (POST/GET/DELETE/SUBSCRIBE) */
int test_gnmi_subscribe_http(void)
{
    danos_gnmi_subscribe_init();
    assert(danos_gnmi_subscribe_count() == 0);

    /* POST /gnmi/subscribe → create subscription with default mask */
    char *resp = danos_gnmi_handle_request("POST", "/gnmi/subscribe", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "subscribed") != NULL);
    /* Extract sub_id from response */
    uint64_t sub_id = 0;
    const char *p = strstr(resp, "sub_id");
    if (p) {
        p = strchr(p, ':');
        if (p) sub_id = (uint64_t)strtoull(p + 1, NULL, 10);
    }
    assert(sub_id > 0);
    free(resp);
    assert(danos_gnmi_subscribe_count() == 1);

    /* POST /gnmi/subscribe?obj_type=route&mask=0x1 → filtered subscription */
    resp = danos_gnmi_handle_request("POST",
            "/gnmi/subscribe?obj_type=route&mask=0x1", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "subscribed") != NULL);
    free(resp);
    assert(danos_gnmi_subscribe_count() == 2);

    /* SUBSCRIBE method (gNMI native) */
    resp = danos_gnmi_handle_request("SUBSCRIBE",
            "/gnmi/subscribe?obj_type=interface", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "subscribed") != NULL);
    free(resp);
    assert(danos_gnmi_subscribe_count() == 3);

    /* GET /gnmi/subscribe/<id> → poll (empty queue → "[]") */
    char path[64];
    snprintf(path, sizeof(path), "/gnmi/subscribe/%llu",
             (unsigned long long)sub_id);
    resp = danos_gnmi_handle_request("GET", path, NULL);
    assert(resp != NULL);
    assert(strcmp(resp, "[]") == 0);
    free(resp);

    /* GET /gnmi/subscribe/0 → invalid sub_id */
    resp = danos_gnmi_handle_request("GET", "/gnmi/subscribe/0", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* DELETE /gnmi/subscribe/<id> → unsubscribe */
    resp = danos_gnmi_handle_request("DELETE", path, NULL);
    assert(resp != NULL);
    assert(strstr(resp, "unsubscribed") != NULL);
    free(resp);
    assert(danos_gnmi_subscribe_count() == 2);

    /* DELETE non-existent sub_id */
    resp = danos_gnmi_handle_request("DELETE", "/gnmi/subscribe/99999", NULL);
    assert(resp != NULL);
    assert(strstr(resp, "error") != NULL);
    free(resp);

    /* Cleanup remaining subscriptions */
    danos_gnmi_subscribe_fini();
    assert(danos_gnmi_subscribe_count() == 0);

    printf("[PASS] test_gnmi_subscribe_http: POST/GET/DELETE/SUBSCRIBE dispatch\n");
    return 0;
}

/* v0.2: gNMI Subscribe end-to-end via HTTP: publish event → poll via GET */
int test_gnmi_subscribe_http_e2e(void)
{
    danos_gnmi_subscribe_init();

    /* Create subscription via POST */
    char *resp = danos_gnmi_handle_request("POST", "/gnmi/subscribe", NULL);
    assert(resp != NULL);
    uint64_t sub_id = 0;
    const char *p = strstr(resp, "sub_id");
    if (p) {
        p = strchr(p, ':');
        if (p) sub_id = (uint64_t)strtoull(p + 1, NULL, 10);
    }
    assert(sub_id > 0);
    free(resp);

    /* Publish event via event bus */
    danos_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DANOS_EVENT_OBJ_CREATED;
    ev.obj_type = DANOS_OBJ_ACL;
    ev.obj_id = 7;
    ev.timestamp_ns = 999;
    danos_event_publish(&ev);

    /* Poll via GET /gnmi/subscribe/<id> */
    char path[64];
    snprintf(path, sizeof(path), "/gnmi/subscribe/%llu",
             (unsigned long long)sub_id);
    resp = danos_gnmi_handle_request("GET", path, NULL);
    assert(resp != NULL);
    assert(strstr(resp, "created") != NULL);
    assert(strstr(resp, "acl") != NULL);
    assert(strstr(resp, "7") != NULL);
    free(resp);

    /* Second poll: empty */
    resp = danos_gnmi_handle_request("GET", path, NULL);
    assert(resp != NULL);
    assert(strcmp(resp, "[]") == 0);
    free(resp);

    danos_gnmi_unsubscribe(sub_id);
    danos_gnmi_subscribe_fini();
    printf("[PASS] test_gnmi_subscribe_http_e2e: publish → HTTP poll\n");
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
    if (test_gnmi_subscribe_e2e() != 0) failed++;
    if (test_gnmi_acl() != 0) failed++;
    if (test_gnmi_qos() != 0) failed++;
    if (test_gnmi_subscribe_http() != 0) failed++;
    if (test_gnmi_subscribe_http_e2e() != 0) failed++;
    printf("=== gnmi_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
