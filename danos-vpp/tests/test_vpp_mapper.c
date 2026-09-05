/*
 * Test: VPP Mapper (D2-D6)
 * Verify DPA objects are correctly mapped to VPP API calls.
 */
#include "../src/mapper/vpp_mapper.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int test_d2_iface(void)
{
    vpp_mapper_reset_stats();

    danos_iface_t iface;
    memset(&iface, 0, sizeof(iface));
    iface.ifindex = 1;
    strcpy(iface.name, "eth0");
    iface.mtu = 1500;
    iface.admin_up = true;

    assert(vpp_map_iface_create(NULL, &iface) == DANOS_OK);

    /* Invalid: ifindex=0 */
    iface.ifindex = 0;
    assert(vpp_map_iface_create(NULL, &iface) == DANOS_ERR_INVALID_ARG);

    /* Invalid: empty name */
    iface.ifindex = 1;
    iface.name[0] = '\0';
    assert(vpp_map_iface_create(NULL, &iface) == DANOS_ERR_INVALID_ARG);

    /* Invalid: MTU too small */
    strcpy(iface.name, "eth0");
    iface.mtu = 10;
    assert(vpp_map_iface_create(NULL, &iface) == DANOS_ERR_INVALID_ARG);

    /* Delete */
    assert(vpp_map_iface_delete(NULL, 1) == DANOS_OK);
    assert(vpp_map_iface_delete(NULL, 0) == DANOS_ERR_INVALID_ARG);

    vpp_api_stats_t stats;
    vpp_mapper_get_stats(&stats);
    /* Only the valid create should count */
    assert(stats.sw_interface_create == 1);
    assert(stats.sw_interface_delete == 1);

    printf("[PASS] test_d2_iface: DPA Interface → VPP sw_interface\n");
    return 0;
}

int test_d3_vrf(void)
{
    vpp_mapper_reset_stats();

    danos_vrf_t vrf;
    memset(&vrf, 0, sizeof(vrf));
    vrf.vrf_id = 100;
    strcpy(vrf.name, "VRF100");

    assert(vpp_map_vrf_create(NULL, &vrf) == DANOS_OK);

    /* Cannot create default VRF */
    vrf.vrf_id = 0;
    assert(vpp_map_vrf_create(NULL, &vrf) == DANOS_ERR_INVALID_ARG);

    /* Delete */
    assert(vpp_map_vrf_delete(NULL, 100) == DANOS_OK);
    assert(vpp_map_vrf_delete(NULL, 0) == DANOS_ERR_INVALID_ARG);

    printf("[PASS] test_d3_vrf: DPA VRF → VPP FIB table\n");
    return 0;
}

int test_d4_route_nh(void)
{
    vpp_mapper_reset_stats();

    /* NH */
    danos_nexthop_t nh;
    memset(&nh, 0, sizeof(nh));
    nh.id = 1;
    nh.gateway.af = DANOS_AF_IPV4;
    nh.gateway.addr[0] = 10; nh.gateway.addr[3] = 1;
    nh.ifindex = 1;
    nh.weight = 1;
    assert(vpp_map_nh_create(NULL, &nh) == DANOS_OK);

    /* NHGroup */
    danos_nhgroup_t grp;
    memset(&grp, 0, sizeof(grp));
    grp.id = 1;
    grp.nh_count = 1;
    grp.nh_ids[0] = 1;
    assert(vpp_map_nhgroup_create(NULL, &grp) == DANOS_OK);

    /* NHGroup with too many NHs */
    grp.nh_count = 65;
    assert(vpp_map_nhgroup_create(NULL, &grp) == DANOS_ERR_INVALID_ARG);

    /* Route */
    danos_route_t route;
    memset(&route, 0, sizeof(route));
    route.vrf_id = 0;
    route.prefix.prefix_len = 24;
    route.prefix.addr.af = DANOS_AF_IPV4;
    route.prefix.addr.addr[0] = 10;
    route.nhgroup_id = 1;
    route.protocol = DANOS_ROUTE_PROTO_STATIC;
    assert(vpp_map_route_create(NULL, &route) == DANOS_OK);
    assert(vpp_map_route_delete(NULL, &route) == DANOS_OK);

    printf("[PASS] test_d4_route_nh: DPA Route/NH/NHGroup → VPP ip_route\n");
    return 0;
}

int test_d5_acl(void)
{
    vpp_mapper_reset_stats();

    danos_acl_table_t tbl;
    memset(&tbl, 0, sizeof(tbl));
    tbl.table_id = 1;
    strcpy(tbl.name, "ACL1");
    assert(vpp_map_acl_table_create(NULL, &tbl) == DANOS_OK);

    danos_acl_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.rule_id = 1;
    rule.act.action = DANOS_ACL_ACTION_PERMIT;
    rule.priority = 100;
    assert(vpp_map_acl_rule_add(NULL, 1, &rule) == DANOS_OK);

    /* Invalid action */
    rule.act.action = (danos_acl_action_t)99;
    assert(vpp_map_acl_rule_add(NULL, 1, &rule) == DANOS_ERR_INVALID_ARG);

    printf("[PASS] test_d5_acl: DPA ACL → VPP classify\n");
    return 0;
}

int test_d6_qos(void)
{
    vpp_mapper_reset_stats();

    danos_qos_policy_t p;
    memset(&p, 0, sizeof(p));
    p.policy_id = 1;
    p.cir_bps = 1000000;  /* 1 Mbps */
    p.cb_bytes = 2000;
    assert(vpp_map_qos_policy_create(NULL, &p) == DANOS_OK);

    /* Invalid: CIR=0 */
    p.cir_bps = 0;
    assert(vpp_map_qos_policy_create(NULL, &p) == DANOS_ERR_INVALID_ARG);

    /* Dual rate: PIR < CIR invalid */
    p.cir_bps = 1000000;
    p.pir_bps = 500000;
    assert(vpp_map_qos_policy_create(NULL, &p) == DANOS_ERR_INVALID_ARG);

    /* Dual rate: PIR >= CIR valid */
    p.pir_bps = 2000000;
    assert(vpp_map_qos_policy_create(NULL, &p) == DANOS_OK);

    assert(vpp_map_qos_policy_delete(NULL, 1) == DANOS_OK);
    assert(vpp_map_qos_policy_delete(NULL, 0) == DANOS_ERR_INVALID_ARG);

    printf("[PASS] test_d6_qos: DPA QoS → VPP policer\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_d2_iface() != 0) failed++;
    if (test_d3_vrf() != 0) failed++;
    if (test_d4_route_nh() != 0) failed++;
    if (test_d5_acl() != 0) failed++;
    if (test_d6_qos() != 0) failed++;
    printf("=== vpp_mapper_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
