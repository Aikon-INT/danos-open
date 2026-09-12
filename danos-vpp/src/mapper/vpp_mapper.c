/*
 * DANOS-Open VPP Backend: DPA → VPP Mapper Implementation (D2-D6)
 *
 * Implements the mapping from DPA objects to VPP binary API calls.
 * In production, this would call VPP's binary API via shared memory.
 * For v0.1, we implement the mapping logic with a mock VPP API layer
 * that records calls for verification.
 */

#include "vpp_mapper.h"
#include "../api/vpp_msgs.h"
#include "../api/vpp_api.h"
#include <string.h>
#include <stdio.h>

/* Global stats for mock mode */
static vpp_api_stats_t g_stats;

void vpp_mapper_get_stats(vpp_api_stats_t *out)
{
    if (out) *out = g_stats;
}

void vpp_mapper_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

/* =========================================================================
 * D2: Interface → VPP sw_interface
 *
 * VPP API calls:
 *   - sw_interface_tap_create (for TAP type)
 *   - sw_interface_set_flags (admin up/down)
 *   - sw_interface_set_mtu
 * ========================================================================= */
danos_status_t vpp_map_iface_create(vpp_api_ctx_t *ctx, const danos_iface_t *iface)
{
    (void)ctx;
    if (!iface) return DANOS_ERR_INVALID_ARG;
    if (iface->ifindex == 0) return DANOS_ERR_INVALID_ARG;
    if (iface->name[0] == '\0') return DANOS_ERR_INVALID_ARG;

    /* Validate MTU range */
    if (iface->mtu < 68 || iface->mtu > 9000) {
        return DANOS_ERR_INVALID_ARG;
    }

    g_stats.sw_interface_create++;
    /* Real backend: bring the interface admin-up once created */
    if (danos_vpp_api_is_connected() && !danos_vpp_api_is_mock()) {
        return vpp_msg_sw_interface_set_flags(iface->ifindex, true);
    }
    return DANOS_OK;
}

danos_status_t vpp_map_iface_delete(vpp_api_ctx_t *ctx, danos_ifindex_t ifindex)
{
    (void)ctx;
    if (ifindex == 0) return DANOS_ERR_INVALID_ARG;
    g_stats.sw_interface_delete++;
    return DANOS_OK;
}

/* =========================================================================
 * D3: VRF → VPP FIB table
 *
 * VPP API calls:
 *   - ip_table_add_del (creates VRF as FIB table)
 * ========================================================================= */
danos_status_t vpp_map_vrf_create(vpp_api_ctx_t *ctx, const danos_vrf_t *vrf)
{
    (void)ctx;
    if (!vrf) return DANOS_ERR_INVALID_ARG;
    if (vrf->vrf_id == 0) return DANOS_ERR_INVALID_ARG;  /* VRF 0 is default */
    g_stats.ip_table_create++;
    if (danos_vpp_api_is_connected() && !danos_vpp_api_is_mock()) {
        return vpp_msg_ip_table_add_del(vrf->vrf_id, false, NULL, true);
    }
    return DANOS_OK;
}

danos_status_t vpp_map_vrf_delete(vpp_api_ctx_t *ctx, danos_vrf_id_t vrf_id)
{
    (void)ctx;
    if (vrf_id == 0) return DANOS_ERR_INVALID_ARG;  /* cannot delete default VRF */
    g_stats.ip_table_delete++;
    return DANOS_OK;
}

/* =========================================================================
 * D4: Route + NH + NHGroup → VPP ip_route
 *
 * VPP API calls:
 *   - ip_route_add_del (with path list for ECMP)
 *   - ip_neighbor_add_del (for ARP/ND)
 * ========================================================================= */
danos_status_t vpp_map_route_create(vpp_api_ctx_t *ctx, const danos_route_t *route)
{
    (void)ctx;
    if (!route) return DANOS_ERR_INVALID_ARG;
    if (route->prefix.prefix_len > 128) return DANOS_ERR_INVALID_ARG;
    if (route->nhgroup_id == 0) return DANOS_ERR_INVALID_ARG;
    g_stats.ip_route_add++;
    return DANOS_OK;
}

danos_status_t vpp_map_route_delete(vpp_api_ctx_t *ctx, const danos_route_t *route)
{
    (void)ctx;
    if (!route) return DANOS_ERR_INVALID_ARG;
    g_stats.ip_route_del++;
    return DANOS_OK;
}

danos_status_t vpp_map_nh_create(vpp_api_ctx_t *ctx, const danos_nexthop_t *nh)
{
    (void)ctx;
    if (!nh) return DANOS_ERR_INVALID_ARG;
    if (nh->id == 0) return DANOS_ERR_INVALID_ARG;
    g_stats.ip_neighbor_add++;
    return DANOS_OK;
}

danos_status_t vpp_map_nhgroup_create(vpp_api_ctx_t *ctx, const danos_nhgroup_t *grp)
{
    (void)ctx;
    if (!grp) return DANOS_ERR_INVALID_ARG;
    if (grp->id == 0) return DANOS_ERR_INVALID_ARG;
    if (grp->nh_count == 0 || grp->nh_count > 64) {
        return DANOS_ERR_INVALID_ARG;
    }
    /* Verify all NH IDs are non-zero */
    for (uint32_t i = 0; i < grp->nh_count; i++) {
        if (grp->nh_ids[i] == 0) return DANOS_ERR_INVALID_ARG;
    }
    g_stats.ip_route_add++;  /* NHGroup maps to multi-path route in VPP */
    return DANOS_OK;
}

/* =========================================================================
 * D5: ACL → VPP classify table + session
 *
 * VPP API calls:
 *   - classify_add_del_table (creates match mask)
 *   - classify_add_del_session (adds rule to table)
 * ========================================================================= */
danos_status_t vpp_map_acl_table_create(vpp_api_ctx_t *ctx, const danos_acl_table_t *tbl)
{
    (void)ctx;
    if (!tbl) return DANOS_ERR_INVALID_ARG;
    if (tbl->table_id == 0) return DANOS_ERR_INVALID_ARG;
    g_stats.classify_add_table++;
    return DANOS_OK;
}

danos_status_t vpp_map_acl_rule_add(vpp_api_ctx_t *ctx, danos_obj_id_t table_id,
                                    const danos_acl_rule_t *rule)
{
    (void)ctx;
    if (table_id == 0 || !rule) return DANOS_ERR_INVALID_ARG;
    if (rule->rule_id == 0) return DANOS_ERR_INVALID_ARG;
    /* Validate action */
    if (rule->act.action < DANOS_ACL_ACTION_PERMIT ||
        rule->act.action > DANOS_ACL_ACTION_SET_DSCP) {
        return DANOS_ERR_INVALID_ARG;
    }
    g_stats.classify_add_del_session++;
    return DANOS_OK;
}

/* =========================================================================
 * D6: QoS → VPP policer
 *
 * VPP API calls:
 *   - policer_add_del (single-rate or dual-rate)
 * ========================================================================= */
danos_status_t vpp_map_qos_policy_create(vpp_api_ctx_t *ctx, const danos_qos_policy_t *p)
{
    (void)ctx;
    if (!p) return DANOS_ERR_INVALID_ARG;
    if (p->policy_id == 0) return DANOS_ERR_INVALID_ARG;

    /* Validate CIR > 0 */
    if (p->cir_bps == 0) return DANOS_ERR_INVALID_ARG;

    /* For dual-rate (pir_bps > 0), PIR must be >= CIR */
    if (p->pir_bps > 0 && p->pir_bps < p->cir_bps) {
        return DANOS_ERR_INVALID_ARG;
    }

    g_stats.policer_add++;
    return DANOS_OK;
}

danos_status_t vpp_map_qos_policy_delete(vpp_api_ctx_t *ctx, danos_obj_id_t policy_id)
{
    (void)ctx;
    if (policy_id == 0) return DANOS_ERR_INVALID_ARG;
    g_stats.policer_del++;
    return DANOS_OK;
}
