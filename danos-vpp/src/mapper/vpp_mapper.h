/*
 * DANOS-Open VPP Backend: DPA → VPP Mapper Interface (D2-D6)
 *
 * Defines the mapping from DPA objects to VPP binary API calls.
 * In production, these call VPP's shared-memory binary API.
 * For v0.1, we implement the mapping logic and verify it with
 * mock VPP API calls.
 */

#ifndef DANOS_VPP_MAPPER_H__
#define DANOS_VPP_MAPPER_H__

#include <danos/dpa.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VPP API context (opaque) */
typedef struct vpp_api_ctx vpp_api_ctx_t;

/* VPP API call counts (for testing/mock) */
typedef struct {
    uint64_t sw_interface_create;
    uint64_t sw_interface_delete;
    uint64_t ip_table_create;
    uint64_t ip_table_delete;
    uint64_t ip_route_add;
    uint64_t ip_route_del;
    uint64_t ip_neighbor_add;
    uint64_t ip_neighbor_del;
    uint64_t classify_add_table;
    uint64_t classify_add_del_session;
    uint64_t policer_add;
    uint64_t policer_del;
} vpp_api_stats_t;

/* Mapper functions: translate DPA object to VPP API call sequence.
 * Each returns DANOS_OK on success, error code on failure.
 * ctx is the VPP API connection (NULL for mock/test mode). */

/* D2: Interface → VPP sw_interface */
danos_status_t vpp_map_iface_create(vpp_api_ctx_t *ctx, const danos_iface_t *iface);
danos_status_t vpp_map_iface_delete(vpp_api_ctx_t *ctx, danos_ifindex_t ifindex);

/* D3: VRF → VPP FIB table */
danos_status_t vpp_map_vrf_create(vpp_api_ctx_t *ctx, const danos_vrf_t *vrf);
danos_status_t vpp_map_vrf_delete(vpp_api_ctx_t *ctx, danos_vrf_id_t vrf_id);

/* D4: Route + NH + NHGroup → VPP ip_route */
danos_status_t vpp_map_route_create(vpp_api_ctx_t *ctx, const danos_route_t *route);
danos_status_t vpp_map_route_delete(vpp_api_ctx_t *ctx, const danos_route_t *route);
danos_status_t vpp_map_nh_create(vpp_api_ctx_t *ctx, const danos_nexthop_t *nh);
danos_status_t vpp_map_nhgroup_create(vpp_api_ctx_t *ctx, const danos_nhgroup_t *grp);

/* D5: ACL → VPP classify table + session */
danos_status_t vpp_map_acl_table_create(vpp_api_ctx_t *ctx, const danos_acl_table_t *tbl);
danos_status_t vpp_map_acl_rule_add(vpp_api_ctx_t *ctx, danos_obj_id_t table_id,
                                    const danos_acl_rule_t *rule);

/* D6: QoS → VPP policer */
danos_status_t vpp_map_qos_policy_create(vpp_api_ctx_t *ctx, const danos_qos_policy_t *p);
danos_status_t vpp_map_qos_policy_delete(vpp_api_ctx_t *ctx, danos_obj_id_t policy_id);

/* Get API call statistics (for testing) */
void vpp_mapper_get_stats(vpp_api_stats_t *out);
void vpp_mapper_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_VPP_MAPPER_H__ */
