/*
 * DANOS-Open VPP Backend: Typed Binary API Messages (v0.3)
 *
 * Encodes the subset of VPP binary API messages needed by the DPA
 * mapper. Field order and types follow FDio VPP master .api files:
 *
 *   vnet/interface.api      sw_interface_set_flags (autoreply, v3.2.5)
 *   vnet/ip/ip.api          ip_route_add_del, ip_table_add_del
 *   vnet/ip/ip_types.api    address/prefix typedefs
 *   vnet/fib/fib_types.api  fib_path, fib_path_nh typedefs
 *   vnet/ip-neighbor.api    ip_neighbor_add_del
 *   vlibmemory/memclnt.api  control_ping
 *
 * All functions resolve the message name to its dynamic msg_id via the
 * handshake table, encode the struct big-endian/packed, and (except
 * the _encode variants) run a request/reply transaction. The reply
 * retval is returned as a DANOS status.
 */

#ifndef DANOS_VPP_MSGS_H__
#define DANOS_VPP_MSGS_H__

#include <danos/dpa.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- address helpers ------------------------------------------------- */

typedef struct {
    bool is_ipv6;
    uint8_t addr[16];   /* 4 bytes used for IPv4 */
} vpp_ip_t;

typedef struct {
    vpp_ip_t addr;
    uint8_t  len;       /* prefix length */
} vpp_prefix_t;

/* --- encode-only variants (used by tests to verify wire layout) ------ */

/* ip_route_add_del body (after client_index+context):
 * bool is_add; bool is_multipath; ip_route route;
 * ip_route { u32 table_id; u32 stats_index; prefix prefix;
 *            u8 n_paths; fib_path paths[n_paths]; }
 * fib_path { u32 sw_if_index; u32 table_id; u32 rpf_id; u8 weight;
 *            u8 preference; u8 type; u8 flags; u8 proto;
 *            fib_path_nh { addr(16) | via_label u32 | obj_id u32 |
 *                          classify_table_index u32 };
 *            u8 n_labels; fib_mpls_label label_stack[16]; }
 * fib_mpls_label { u8 is_uniform; u32 label; u8 ttl; u8 exp; } (7 bytes)
 */
int vpp_encode_ip_route_add_del(uint8_t is_add, uint32_t table_id,
                                const vpp_prefix_t *prefix,
                                uint32_t n_paths, const vpp_ip_t *nhs,
                                const uint32_t *nh_ifs,
                                uint8_t *out, uint32_t out_size);

/* Encode one fib_path with a next-hop address. */
int vpp_encode_fib_path(uint8_t *out, uint32_t out_size,
                        uint32_t sw_if_index, uint32_t table_id,
                        const vpp_ip_t *nh);

/* ip_table_add_del body: bool is_add; ip_table table;
 * ip_table { u32 table_id; bool is_ip6; string name[64]; } */
int vpp_encode_ip_table_add_del(uint8_t is_add, uint32_t table_id,
                                bool is_ip6, const char *name,
                                uint8_t *out, uint32_t out_size);

/* sw_interface_set_flags body: u32 sw_if_index; u32 flags;
 * only IF_STATUS_API_FLAG_ADMIN_UP (0x1) is used. */
int vpp_encode_sw_interface_set_flags(uint32_t sw_if_index, bool admin_up,
                                      uint8_t *out, uint32_t out_size);

/* ip_neighbor_add_del body: bool is_add; bool is_del_all;
 * ip_neighbor { u32 sw_if_index; u8 flags; mac mac[6]; address ip; } */
int vpp_encode_ip_neighbor_add_del(uint8_t is_add, uint32_t sw_if_index,
                                   const uint8_t mac[6], const vpp_ip_t *ip,
                                   uint8_t *out, uint32_t out_size);

/* policer_add_del (plugins/policer/policer.api v3.0.0) — CoPP (P2) */
int vpp_encode_policer_add_del(uint8_t is_add, const char *name,
                               uint64_t cir_kbps, uint64_t eir_kbps,
                               uint64_t cb_bytes, uint64_t eb_bytes,
                               uint8_t conform_action, uint8_t conform_dscp,
                               uint8_t exceed_action, uint8_t exceed_dscp,
                               uint8_t violate_action, uint8_t violate_dscp,
                               uint8_t *out, uint32_t out_size);
danos_status_t vpp_msg_policer_add_del(bool is_add, const char *name,
                                       const danos_qos_policy_t *p);

/* --- request/reply transactions --------------------------------------- */

danos_status_t vpp_msg_sw_interface_set_flags(uint32_t sw_if_index, bool admin_up);
danos_status_t vpp_msg_ip_table_add_del(uint32_t table_id, bool is_ip6,
                                        const char *name, bool is_add);
danos_status_t vpp_msg_ip_route_add_del(bool is_add, uint32_t table_id,
                                        const vpp_prefix_t *prefix,
                                        uint32_t n_paths, const vpp_ip_t *nhs,
                                        const uint32_t *nh_ifs);
danos_status_t vpp_msg_ip_neighbor_add_del(bool is_add, uint32_t sw_if_index,
                                           const uint8_t mac[6],
                                           const vpp_ip_t *ip);
danos_status_t vpp_msg_control_ping(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_VPP_MSGS_H__ */
