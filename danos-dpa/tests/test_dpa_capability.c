/*
 * Test: DPA capability query (A5)
 */
#include <danos/dpa.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void danos_backend_reset(void);
extern danos_status_t danos_backend_register(const danos_backend_info_t *info);

static int test_capability_query(void)
{
    danos_backend_reset();

    /* Register VPP backend with subset of capabilities */
    static const char *route_features[] = {"ipv4", "ipv6", "multipath"};
    static const char *evpn_features[]  = {"type2", "type3", "type5", "irb", "mh"};
    static const danos_capability_t vpp_caps[] = {
        {DANOS_OBJ_ROUTE, true, 1000000, 3, route_features, NULL},
        {DANOS_OBJ_IFACE, true, 1024,    0, NULL,           NULL},
        {DANOS_OBJ_VRF,   true, 4096,    0, NULL,           NULL},
        {DANOS_OBJ_EVPN,  true, 4096,    5, evpn_features,  NULL},
        {DANOS_OBJ_MULTICAST, false, 0,  0, NULL,           NULL},
    };
    danos_backend_info_t vpp = {0};
    strncpy(vpp.name, "vpp", sizeof(vpp.name) - 1);
    vpp.api_version = danos_dpa_get_version();
    vpp.cap_count = 5;
    vpp.caps = vpp_caps;
    assert(danos_backend_register(&vpp) == DANOS_OK);

    /* Query: VPP supports ROUTE */
    danos_capability_t cap;
    danos_status_t st = danos_capability_query("vpp", DANOS_OBJ_ROUTE, &cap);
    assert(st == DANOS_OK);
    assert(cap.supported == true);
    assert(cap.max_count == 1000000);
    assert(cap.features_count == 3);

    /* Query: VPP does not support MULTICAST */
    st = danos_capability_query("vpp", DANOS_OBJ_MULTICAST, &cap);
    assert(st == DANOS_OK);
    assert(cap.supported == false);

    /* Query: unknown backend */
    st = danos_capability_query("nonexistent", DANOS_OBJ_ROUTE, &cap);
    assert(st == DANOS_ERR_NOT_SUPPORTED);

    /* Query: any backend that supports IFACE */
    st = danos_capability_query(NULL, DANOS_OBJ_IFACE, &cap);
    assert(st == DANOS_OK);
    assert(cap.supported == true);

    /* Query: any backend for MPLS (not registered) */
    st = danos_capability_query(NULL, DANOS_OBJ_MPLS_LSP, &cap);
    assert(st == DANOS_ERR_NOT_SUPPORTED);

    printf("[PASS] test_capability_query: capability lookup works\n");
    return 0;
}

int main(void)
{
    if (test_capability_query() != 0) return 1;
    printf("=== test_dpa_capability: all passed ===\n");
    return 0;
}
