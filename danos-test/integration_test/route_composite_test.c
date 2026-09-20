/*
 * v0.12: northbound composite route write end to end.
 *
 * gNMI Set /routes/route[prefix=X] -> NH+Group+Route in one tx ->
 * programming pipeline -> mock kernel FIB -> Get verification ->
 * idempotent re-set -> cascade delete.
 */

#include <danos/core/backend_ops.h>
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include "../../danos-mgmt/src/gnmi/gnmi_grpc.h"
#include "../../danos-mgmt/src/gnmi/gnmi_proto.h"
#include "../../danos-mgmt/src/gnmi/model_routes.h"
#include "../../danos-mgmt/src/gnmi/hpack.h"
#include "../../danos-netlink/danos_netlink.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int write_set_route(const char *prefix, const char *gw,
                           uint8_t *req, size_t req_cap)
{
    gnmi_pb_t w;
    gnmi_pb_init(&w, req, (uint32_t)req_cap);
    gnmi_update_t u;
    memset(&u, 0, sizeof(u));
    char p[96];
    snprintf(p, sizeof(p), "routes/route[prefix=%s]", prefix);
    assert(gnmi_path_from_str(&u.path, p));
    u.val.kind = GNMI_VAL_JSON_IETF;
    char val[128];
    snprintf(val, sizeof(val), "{\"gateway\":\"%s\",\"oif\":1,\"vrf\":0}", gw);
    strcpy(u.val.s, val);
    size_t us = gnmi_pb_begin_nested(&w, 4);
    gnmi_encode_path(&w, 1, &u.path);
    gnmi_encode_typed_value(&w, 3, &u.val);
    gnmi_pb_end_nested(&w, us);
    return (int)w.len;
}

static int write_set_ecmp_route(const char *prefix, uint8_t *req, size_t cap)
{
    gnmi_pb_t w; gnmi_pb_init(&w, req, (uint32_t)cap);
    gnmi_update_t u; memset(&u, 0, sizeof(u));
    char p[96]; snprintf(p, sizeof(p), "routes/route[prefix=%s]", prefix);
    assert(gnmi_path_from_str(&u.path, p));
    u.val.kind = GNMI_VAL_JSON_IETF;
    strcpy(u.val.s, "{\"gateways\":[\"10.0.0.2\",\"10.0.0.3\"],\"oif\":1,\"vrf\":0}");
    size_t us = gnmi_pb_begin_nested(&w, 4);
    gnmi_encode_path(&w, 1, &u.path);
    gnmi_encode_typed_value(&w, 3, &u.val);
    gnmi_pb_end_nested(&w, us);
    return (int)w.len;
}

int main(void)
{
    if (!g_default_store) g_default_store = danos_object_store_create(256);

    /* mock kernel backend */
    assert(danos_netlink_init(false) == 0);
    danos_netlink_register_backend();

    uint8_t req[512], resp[4096];
    uint8_t dst[4] = {10, 99, 0, 0};

    /* 1. gNMI Set route */
    int rlen = write_set_route("10.99.0.0/24", "10.0.0.2", req, sizeof(req));
    rlen = gnmi_handle_set(NULL, req, (size_t)rlen, resp, sizeof(resp));
    assert(rlen >= 0);

    /* 2. pipeline programs the mock kernel FIB */
    uint64_t attempted = 0, failed = 0;
    assert(danos_programming_run(&attempted, &failed) >= 1);
    assert(failed == 0);
    assert(danos_netlink_mock_route_count() == 1);
    assert(danos_netlink_mock_route_exists(dst, 24, 0));

    /* 3. gNMI Get returns prefix + gateway */
    uint8_t greq[128];
    gnmi_pb_t gw;
    gnmi_pb_init(&gw, greq, sizeof(greq));
    gnmi_path_t gp;
    assert(gnmi_path_from_str(&gp, "routes/route[prefix=10.99.0.0/24]"));
    gnmi_encode_path(&gw, 2, &gp);
    rlen = gnmi_handle_get(NULL, greq, gw.len, resp, sizeof(resp));
    assert(rlen > 0);
    bool saw_gw = false;
    for (int i = 0; i < rlen - 8; i++)
        if (memcmp(resp + i, "10.0.0.2", 8) == 0) saw_gw = true;
    assert(saw_gw);

    /* 4. idempotent re-set: no duplicate FIB entry */
    rlen = write_set_route("10.99.0.0/24", "10.0.0.2", req, sizeof(req));
    rlen = gnmi_handle_set(NULL, req, (size_t)rlen, resp, sizeof(resp));
    assert(rlen >= 0);
    assert(danos_netlink_mock_route_count() == 1);

    /* 5. gNMI delete (Set with delete=2 field) -> cascade withdraw */
    uint8_t dreq[128];
    gnmi_pb_t dw;
    gnmi_pb_init(&dw, dreq, sizeof(dreq));
    gnmi_path_t dp;
    assert(gnmi_path_from_str(&dp, "routes/route[prefix=10.99.0.0/24]"));
    gnmi_encode_path(&dw, 2, &dp);
    rlen = gnmi_handle_set(NULL, dreq, dw.len, resp, sizeof(resp));
    assert(rlen >= 0);
    /* sweep withdraws the kernel entry */
    uint64_t swept = danos_programming_sweep(&failed);
    assert(swept == 1 && failed == 0);
    assert(!danos_netlink_mock_route_exists(dst, 24, 0));
    /* composite objects removed from desired */
    assert(danos_programming_programmed_count(DANOS_OBJ_ROUTE) == 0);

    /* 6. ECMP composite set creates a multi-member NHGroup. */
    danos_ip_prefix_t ecmp_prefix;
    assert(gnmi_parse_prefix("10.100.0.0/24", &ecmp_prefix) == DANOS_OK);
    rlen = write_set_ecmp_route("10.100.0.0/24", req, sizeof(req));
    assert(gnmi_handle_set(NULL, req, (size_t)rlen, resp, sizeof(resp)) >= 0);
    assert(danos_object_count(g_default_store, DANOS_OBJ_NEXTHOP) == 2);
    assert(danos_object_count(g_default_store, DANOS_OBJ_NHGROUP) == 1);
    assert(danos_programming_run(&attempted, &failed) >= 1 && failed == 0);
    assert(danos_netlink_mock_route_count() == 1);
    assert(gnmi_route_delete(0, &ecmp_prefix) == DANOS_OK);
    assert(danos_programming_sweep(&failed) >= 1 && failed == 0);
    assert(danos_netlink_mock_route_count() == 0);

    printf("=== route_composite_test: ALL PASSED ===\n");
    return 0;
}
