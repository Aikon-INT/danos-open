/*
 * DANOS-Open DPA (Data Plane Abstraction) Public API
 * Version: v0.1
 * License: Apache-2.0
 *
 * This header defines the stable C ABI for DANOS-Open DPA.
 * All objects are managed through explicit transactions.
 * Backends (VPP / OVS-DPDK / P4-DPDK) implement this ABI.
 *
 * Design principles:
 *   1. No proprietary SDK dependency in core.
 *   2. Capability-based: every object type has a capability profile.
 *   3. Transactional: all mutations go through begin/prepare/validate/commit.
 *   4. Backend-agnostic: same API for VPP, OVS, P4, future ASIC.
 *   5. Versioned: semantic versioning + capability negotiation.
 */

#ifndef DANOS_DPA_H__
#define DANOS_DPA_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. Common types
 * ========================================================================= */

typedef uint64_t danos_tx_id_t;       /* Transaction ID (monotonic, WAL-logged) */
typedef uint64_t danos_obj_id_t;      /* Object ID (backend-allocated, unique) */
typedef uint32_t danos_vrf_id_t;      /* VRF ID (0 = default VRF) */
typedef uint32_t danos_ifindex_t;     /* Interface index */
typedef uint32_t danos_vlan_id_t;     /* VLAN ID (1..4094) */
typedef uint32_t danos_mpls_label_t;  /* MPLS label (0..1048575) */
typedef uint32_t danos_vni_t;         /* VXLAN VNI (0..16777215) */

/* IP address family */
typedef enum {
    DANOS_AF_UNSPEC = 0,
    DANOS_AF_IPV4   = 4,
    DANOS_AF_IPV6   = 6,
} danos_af_t;

/* IP address (128-bit unified, IPv4 in lowest 32 bits) */
typedef struct {
    danos_af_t af;
    uint8_t    addr[16];   /* network byte order */
} danos_ip_addr_t;

/* IP prefix */
typedef struct {
    danos_ip_addr_t addr;
    uint8_t         prefix_len;   /* 0..32 for IPv4, 0..128 for IPv6 */
} danos_ip_prefix_t;

/* =========================================================================
 * 2. Status / error codes
 * ========================================================================= */

typedef enum {
    DANOS_OK                  = 0,

    /* Generic errors */
    DANOS_ERR_INVALID_ARG     = 1,   /* Invalid argument */
    DANOS_ERR_NOT_FOUND       = 2,   /* Object not found */
    DANOS_ERR_EXISTS          = 3,   /* Object already exists */
    DANOS_ERR_NO_MEMORY       = 4,   /* Out of memory */
    DANOS_ERR_NO_CAPACITY     = 5,   /* Backend capability/resource exhausted */
    DANOS_ERR_NOT_SUPPORTED   = 6,   /* Backend does not support this object/op */
    DANOS_ERR_PERMISSION      = 7,   /* Authorization denied */

    /* Transaction errors */
    DANOS_ERR_TX_CONFLICT     = 10,  /* Concurrent write conflict */
    DANOS_ERR_TX_TIMEOUT      = 11,  /* Transaction phase timeout */
    DANOS_ERR_TX_ABORTED      = 12,  /* Transaction explicitly aborted */
    DANOS_ERR_TX_ROLLBACK     = 13,  /* Rollback failed (manual repair needed) */
    DANOS_ERR_TX_INVALID      = 14,  /* Transaction state invalid for op */

    /* Backend errors */
    DANOS_ERR_BACKEND_DOWN    = 20,  /* Backend connection lost */
    DANOS_ERR_BACKEND_BUSY    = 21,  /* Backend temporarily busy */
    DANOS_ERR_BACKEND_IO      = 22,  /* Backend I/O error */

    /* Verify errors */
    DANOS_ERR_VERIFY_FAIL     = 30,  /* Programmed != Oper after commit */
    DANOS_ERR_PARTIAL         = 31,  /* Partial success (see tx log) */

    /* Version errors */
    DANOS_ERR_VERSION         = 40,  /* API version mismatch */
    DANOS_ERR_CAPABILITY      = 41,  /* Required capability not advertised */

    /* Internal */
    DANOS_ERR_INTERNAL        = 99,  /* Internal bug, file issue */
} danos_status_t;

/* Human-readable error string. Stable across versions. */
const char *danos_status_str(danos_status_t st);

/* =========================================================================
 * 3. Version & capability negotiation
 * ========================================================================= */

typedef struct {
    uint16_t major;   /* ABI break */
    uint16_t minor;   /* New feature, backward compatible */
    uint16_t patch;   /* Bug fix */
    uint16_t reserved;
} danos_version_t;

/* Object type enumeration (extensible via capability) */
typedef enum {
    DANOS_OBJ_INVALID      = 0,    /* sentinel: "all types" for subscriptions */
    DANOS_OBJ_IFACE        = 1,
    DANOS_OBJ_VLAN         = 2,
    DANOS_OBJ_VRF          = 3,
    DANOS_OBJ_ROUTE        = 4,
    DANOS_OBJ_NEXTHOP      = 5,
    DANOS_OBJ_NHGROUP      = 6,
    DANOS_OBJ_ACL          = 7,
    DANOS_OBJ_QOS          = 8,
    DANOS_OBJ_MPLS_LSP     = 9,
    DANOS_OBJ_TUNNEL       = 10,
    DANOS_OBJ_EVPN         = 11,
    DANOS_OBJ_MULTICAST    = 12,
    DANOS_OBJ_BFD          = 13,
    DANOS_OBJ_MAX
} danos_obj_type_t;

/* Capability descriptor for one object type */
typedef struct {
    danos_obj_type_t type;
    bool             supported;
    uint32_t         max_count;       /* 0 = unlimited */
    uint32_t         features_count;  /* number of feature flags */
    const char     **features;        /* e.g. {"evpn-irb", "evpn-mh", ...} */
    /* Future: per-field constraints as opaque JSON/YANG blob */
    const char      *constraints_json;
} danos_capability_t;

/* Backend descriptor */
typedef struct {
    char             name[64];        /* "vpp", "ovs-dpdk", "p4-dpdk" */
    danos_version_t  api_version;     /* DPA API version backend implements */
    uint32_t         cap_count;
    const danos_capability_t *caps;
} danos_backend_info_t;

/* Get DPA core version */
danos_version_t danos_dpa_get_version(void);

/* Register a backend (called by backend init code). */
danos_status_t danos_backend_register(const danos_backend_info_t *info);

/* Unregister a backend by name. */
danos_status_t danos_backend_unregister(const char *name);

/* Reset backend registry (for testing). */
void danos_backend_reset(void);

/* Get registered backend info (call repeatedly until DANOS_ERR_NOT_FOUND) */
danos_status_t danos_backend_get_info(uint32_t index, danos_backend_info_t *out);

/* Query capability for object type on a specific backend.
 * backend_name = NULL means "any backend that supports it". */
danos_status_t danos_capability_query(const char *backend_name,
                                      danos_obj_type_t type,
                                      danos_capability_t *out);

/* =========================================================================
 * 4. Transaction API
 * ========================================================================= */

typedef enum {
    DANOS_TX_OPEN      = 0,
    DANOS_TX_PREPARE   = 1,
    DANOS_TX_VALIDATE  = 2,
    DANOS_TX_COMMIT    = 3,
    DANOS_TX_VERIFY    = 4,
    DANOS_TX_DONE      = 5,
    DANOS_TX_ABORT     = 6,
    DANOS_TX_ROLLBACK  = 7,
} danos_tx_state_t;

typedef struct {
    danos_tx_id_t    id;
    danos_tx_state_t state;
    uint64_t         start_ns;     /* start timestamp, nanoseconds since epoch */
    uint64_t         deadline_ns;  /* hard deadline */
    const char      *initiator;    /* e.g. "cli", "gnmi", "frr-zebra" */
    void            *_internal;    /* opaque pointer to internal tx record */
} danos_tx_t;

/* Transaction timeouts (configurable via management) */
typedef struct {
    uint64_t prepare_ms;    /* default 100  */
    uint64_t commit_ms;     /* default 500  */
    uint64_t verify_ms;     /* default 1000 */
} danos_tx_timeouts_t;

/* Begin a new transaction. Returns tx handle with unique ID. */
danos_status_t danos_tx_begin(danos_tx_t *tx,
                              const char *initiator,
                              const danos_tx_timeouts_t *timeouts /* NULL=defaults */);

/* Prepare: stage all operations in backend without committing. */
danos_status_t danos_tx_prepare(danos_tx_t *tx);

/* Validate: check staged ops against backend capability & semantic rules. */
danos_status_t danos_tx_validate(danos_tx_t *tx);

/* Commit: apply staged ops to backend (programmed state). */
danos_status_t danos_tx_commit(danos_tx_t *tx);

/* Verify: read back oper state and confirm match with programmed. */
danos_status_t danos_tx_verify(danos_tx_t *tx);

/* Abort: discard staged ops (only valid in OPEN/PREPARE/VALIDATE). */
danos_status_t danos_tx_abort(danos_tx_t *tx);

/* Rollback: undo a committed transaction (best-effort, logged). */
danos_status_t danos_tx_rollback(danos_tx_t *tx);

/* Get current transaction state. */
danos_status_t danos_tx_get_state(const danos_tx_t *tx, danos_tx_state_t *out);

/* Convenience: begin+prepare+validate+commit+verify in one call.
 * Use for simple single-object ops from management plane. */
danos_status_t danos_tx_commit_atomic(danos_tx_t *tx);

/* =========================================================================
 * 5. Object: Interface
 * ========================================================================= */

typedef enum {
    DANOS_IF_TYPE_PHYS    = 1,   /* physical NIC port */
    DANOS_IF_TYPE_SUBIF   = 2,   /* VLAN sub-interface */
    DANOS_IF_TYPE_BOND    = 3,   /* LAG / LACP */
    DANOS_IF_TYPE_LOOPBK  = 4,   /* loopback */
    DANOS_IF_TYPE_VXLAN   = 5,   /* VXLAN tunnel interface */
    DANOS_IF_TYPE_GRE     = 6,   /* GRE tunnel interface */
    DANOS_IF_TYPE_BRIDGE  = 7,   /* L2 bridge domain */
} danos_if_type_t;

typedef struct {
    danos_ifindex_t  ifindex;        /* key */
    char             name[64];
    danos_if_type_t type;
    uint16_t         mtu;            /* default 1500 */
    uint8_t          mac[6];
    bool             admin_up;
    bool             link_up;        /* oper status, read-only */
    /* For sub-interface */
    danos_ifindex_t  parent_ifindex; /* 0 if not subif */
    danos_vlan_id_t  outer_vlan;     /* 0 if none */
    danos_vlan_id_t  inner_vlan;     /* 0 if none (QinQ) */
} danos_iface_t;

danos_status_t danos_iface_create(danos_tx_t *tx, const danos_iface_t *iface);
danos_status_t danos_iface_update(danos_tx_t *tx, const danos_iface_t *iface);
danos_status_t danos_iface_delete(danos_tx_t *tx, danos_ifindex_t ifindex);
danos_status_t danos_iface_read(danos_tx_t *tx, danos_ifindex_t ifindex, danos_iface_t *out);

/* =========================================================================
 * 6. Object: VRF
 * ========================================================================= */

typedef struct {
    danos_vrf_id_t vrf_id;          /* key */
    char           name[64];
    bool           ipv4_active;
    bool           ipv6_active;
} danos_vrf_t;

danos_status_t danos_vrf_create(danos_tx_t *tx, const danos_vrf_t *vrf);
danos_status_t danos_vrf_update(danos_tx_t *tx, const danos_vrf_t *vrf);
danos_status_t danos_vrf_delete(danos_tx_t *tx, danos_vrf_id_t vrf_id);
danos_status_t danos_vrf_read(danos_tx_t *tx, danos_vrf_id_t vrf_id, danos_vrf_t *out);

/* =========================================================================
 * 7. Object: NextHop
 * ========================================================================= */

typedef enum {
    DANOS_NH_FLAG_NONE        = 0,
    DANOS_NH_FLAG_ECMP        = 1u << 0,  /* member of ECMP group */
    DANOS_NH_FLAG_BACKUP      = 1u << 1,  /* backup NH (fast reroute) */
    DANOS_NH_FLAG_MPLS_POP    = 1u << 2,  /* pop MPLS label */
    DANOS_NH_FLAG_MPLS_PUSH   = 1u << 3,  /* push MPLS label stack */
    DANOS_NH_FLAG_TUNNEL      = 1u << 4,  /* via tunnel interface */
} danos_nh_flags_t;

typedef struct {
    danos_obj_id_t   id;             /* key */
    danos_ip_addr_t  gateway;        /* gateway address */
    danos_ifindex_t  ifindex;        /* egress interface */
    uint32_t         weight;         /* for weighted ECMP */
    uint32_t         flags;          /* bitmask of danos_nh_flags_t */
    /* MPLS label stack (for push) */
    uint8_t          label_count;    /* 0..3 */
    danos_mpls_label_t labels[3];
} danos_nexthop_t;

danos_status_t danos_nh_create(danos_tx_t *tx, const danos_nexthop_t *nh);
danos_status_t danos_nh_update(danos_tx_t *tx, const danos_nexthop_t *nh);
danos_status_t danos_nh_delete(danos_tx_t *tx, danos_obj_id_t id);
danos_status_t danos_nh_read(danos_tx_t *tx, danos_obj_id_t id, danos_nexthop_t *out);

/* =========================================================================
 * 8. Object: NextHop Group (ECMP / weighted ECMP / backup)
 * ========================================================================= */

typedef struct {
    danos_obj_id_t  id;              /* key */
    uint32_t        nh_count;
    danos_obj_id_t  nh_ids[64];      /* up to 64 NHs per group */
    uint32_t        flags;           /* future: resilient hashing, etc. */
} danos_nhgroup_t;

danos_status_t danos_nhgroup_create(danos_tx_t *tx, const danos_nhgroup_t *grp);
danos_status_t danos_nhgroup_update(danos_tx_t *tx, const danos_nhgroup_t *grp);
danos_status_t danos_nhgroup_delete(danos_tx_t *tx, danos_obj_id_t id);
danos_status_t danos_nhgroup_read(danos_tx_t *tx, danos_obj_id_t id, danos_nhgroup_t *out);

/* =========================================================================
 * 9. Object: Route
 * ========================================================================= */

typedef enum {
    DANOS_ROUTE_PROTO_UNSPEC  = 0,
    DANOS_ROUTE_PROTO_KERNEL  = 1,
    DANOS_ROUTE_PROTO_STATIC  = 2,
    DANOS_ROUTE_PROTO_BGP     = 3,
    DANOS_ROUTE_PROTO_OSPF    = 4,
    DANOS_ROUTE_PROTO_ISIS    = 5,
    DANOS_ROUTE_PROTO_CONNECTED = 6,
} danos_route_proto_t;

typedef enum {
    DANOS_ROUTE_FLAG_NONE     = 0,
    DANOS_ROUTE_FLAG_BLACKHOLE = 1u << 0,
    DANOS_ROUTE_FLAG_MULTICAST = 1u << 1,
} danos_route_flags_t;

typedef struct {
    danos_vrf_id_t      vrf_id;          /* key part 1 */
    danos_ip_prefix_t   prefix;          /* key part 2 */
    danos_route_proto_t protocol;
    uint32_t            admin_distance;  /* e.g. BGP=20, OSPF=110 */
    uint32_t            metric;
    danos_obj_id_t      nhgroup_id;      /* 0 for blackhole */
    uint32_t            flags;
} danos_route_t;

danos_status_t danos_route_create(danos_tx_t *tx, const danos_route_t *route);
danos_status_t danos_route_update(danos_tx_t *tx, const danos_route_t *route);
danos_status_t danos_route_delete(danos_tx_t *tx,
                                  danos_vrf_id_t vrf_id,
                                  danos_ip_prefix_t prefix,
                                  danos_route_proto_t proto);
danos_status_t danos_route_read(danos_tx_t *tx,
                                danos_vrf_id_t vrf_id,
                                danos_ip_prefix_t prefix,
                                danos_route_proto_t proto,
                                danos_route_t *out);

/* Bulk dump for reconciliation & telemetry. cb returns 0 to continue. */
typedef int (*danos_route_cb_t)(const danos_route_t *route, void *user);
danos_status_t danos_route_dump(danos_tx_t *tx,
                                danos_vrf_id_t vrf_id,
                                danos_route_cb_t cb,
                                void *user);

/* =========================================================================
 * 10. Object: ACL
 * ========================================================================= */

typedef enum {
    DANOS_ACL_FIELD_ETHER_TYPE = 1u << 0,
    DANOS_ACL_FIELD_SRC_MAC    = 1u << 1,
    DANOS_ACL_FIELD_DST_MAC    = 1u << 2,
    DANOS_ACL_FIELD_VLAN_ID    = 1u << 3,
    DANOS_ACL_FIELD_SRC_IP     = 1u << 4,
    DANOS_ACL_FIELD_DST_IP     = 1u << 5,
    DANOS_ACL_FIELD_L4_PROTO   = 1u << 6,
    DANOS_ACL_FIELD_L4_SRC_PORT= 1u << 7,
    DANOS_ACL_FIELD_L4_DST_PORT= 1u << 8,
    DANOS_ACL_FIELD_DSCP       = 1u << 9,
} danos_acl_fields_t;

typedef enum {
    DANOS_ACL_ACTION_PERMIT   = 1,
    DANOS_ACL_ACTION_DENY     = 2,
    DANOS_ACL_ACTION_MIRROR   = 3,
    DANOS_ACL_ACTION_REDIRECT = 4,
    DANOS_ACL_ACTION_POLICE   = 5,
    DANOS_ACL_ACTION_SET_DSCP = 6,
} danos_acl_action_t;

typedef struct {
    uint32_t      fields_mask;       /* which fields are active */
    uint16_t      ether_type;
    uint8_t       src_mac[6];
    uint8_t       dst_mac[6];
    danos_vlan_id_t vlan_id;
    danos_ip_prefix_t src_ip;
    danos_ip_prefix_t dst_ip;
    uint8_t       l4_proto;          /* 6=TCP, 17=UDP */
    uint16_t      l4_src_port_start;
    uint16_t      l4_src_port_end;
    uint16_t      l4_dst_port_start;
    uint16_t      l4_dst_port_end;
    uint8_t       dscp;
} danos_acl_match_t;

typedef struct {
    danos_acl_action_t action;
    danos_ifindex_t    redirect_ifindex;  /* for REDIRECT */
    uint32_t           police_rate_kbps;  /* for POLICE */
    uint8_t            set_dscp;          /* for SET_DSCP */
} danos_acl_action_params_t;

typedef struct {
    danos_obj_id_t           rule_id;     /* key */
    uint32_t                 priority;    /* lower = higher priority */
    danos_acl_match_t        match;
    danos_acl_action_params_t act;
} danos_acl_rule_t;

typedef struct {
    danos_obj_id_t  table_id;            /* key */
    char            name[64];
    danos_ifindex_t bind_ifindex;
    bool            ingress;
} danos_acl_table_t;

danos_status_t danos_acl_table_create(danos_tx_t *tx, const danos_acl_table_t *tbl);
danos_status_t danos_acl_table_delete(danos_tx_t *tx, danos_obj_id_t table_id);
danos_status_t danos_acl_rule_add(danos_tx_t *tx, danos_obj_id_t table_id, const danos_acl_rule_t *rule);
danos_status_t danos_acl_rule_delete(danos_tx_t *tx, danos_obj_id_t table_id, danos_obj_id_t rule_id);

/* =========================================================================
 * 11. Object: QoS (basic in v0.1, HQoS in v0.2+)
 * ========================================================================= */

typedef struct {
    danos_obj_id_t  policy_id;          /* key */
    char            name[64];
    /* v0.1: simple single-rate policer */
    uint64_t        cir_bps;            /* committed information rate */
    uint64_t        cb_bytes;           /* committed burst */
    uint64_t        pir_bps;            /* peak rate (0 = single rate) */
    uint64_t        pb_bytes;           /* peak burst */
    uint8_t         conform_dscp;       /* mark on conform */
    uint8_t         exceed_dscp;        /* mark on exceed */
    uint8_t         violate_dscp;       /* mark on violate */
} danos_qos_policy_t;

danos_status_t danos_qos_policy_create(danos_tx_t *tx, const danos_qos_policy_t *p);
danos_status_t danos_qos_policy_delete(danos_tx_t *tx, danos_obj_id_t policy_id);
danos_status_t danos_qos_policy_bind(danos_tx_t *tx, danos_obj_id_t policy_id, danos_ifindex_t ifindex, bool ingress);

/* =========================================================================
 * 12. Object: MPLS LSP (v0.2+; declared here for ABI stability)
 * ========================================================================= */

typedef enum {
    DANOS_MPLS_TYPE_LDP     = 1,
    DANOS_MPLS_TYPE_RSVP_TE = 2,
    DANOS_MPLS_TYPE_SR      = 3,
    DANOS_MPLS_TYPE_STATIC  = 4,
} danos_mpls_type_t;

typedef struct {
    danos_mpls_label_t in_label;        /* key */
    danos_mpls_type_t  type;
    danos_obj_id_t     nhgroup_id;      /* forwarding NH group */
    bool               php;             /* penultimate hop pop */
    uint8_t            push_label_count;
    danos_mpls_label_t push_labels[3];
} danos_mpls_lsp_t;

/* MPLS LSP CRUD (v0.2). Keyed by in_label. */
danos_status_t danos_mpls_lsp_create(danos_tx_t *tx, const danos_mpls_lsp_t *lsp);
danos_status_t danos_mpls_lsp_update(danos_tx_t *tx, const danos_mpls_lsp_t *lsp);
danos_status_t danos_mpls_lsp_delete(danos_tx_t *tx, danos_mpls_label_t in_label);
danos_status_t danos_mpls_lsp_read(danos_tx_t *tx, danos_mpls_label_t in_label,
                                   danos_mpls_lsp_t *out);

/* =========================================================================
 * 13. Object: Tunnel (v0.2+; declared here for ABI stability)
 * ========================================================================= */

typedef enum {
    DANOS_TUNNEL_VXLAN  = 1,
    DANOS_TUNNEL_GRE    = 2,
    DANOS_TUNNEL_IPIP   = 3,
    DANOS_TUNNEL_SR_TE  = 4,
} danos_tunnel_type_t;

typedef struct {
    danos_obj_id_t      id;             /* key */
    danos_tunnel_type_t type;
    danos_ifindex_t     ifindex;        /* tunnel interface */
    danos_ip_addr_t     src;
    danos_ip_addr_t     dst;
    danos_vni_t         vni;            /* for VXLAN */
    uint32_t            gre_key;        /* for GRE */
} danos_tunnel_t;

/* Tunnel CRUD (v0.2). Keyed by id. */
danos_status_t danos_tunnel_create(danos_tx_t *tx, const danos_tunnel_t *tun);
danos_status_t danos_tunnel_update(danos_tx_t *tx, const danos_tunnel_t *tun);
danos_status_t danos_tunnel_delete(danos_tx_t *tx, danos_obj_id_t id);
danos_status_t danos_tunnel_read(danos_tx_t *tx, danos_obj_id_t id,
                                 danos_tunnel_t *out);

/* =========================================================================
 * 14. Object: EVPN (v0.2+; declared here for ABI stability)
 * ========================================================================= */

typedef struct {
    uint32_t        evi;                /* EVPN Instance ID, key */
    danos_ip_addr_t rd;                 /* Route Distinguisher */
    danos_ip_addr_t rt_import;
    danos_ip_addr_t rt_export;
    danos_vni_t     vni;
    bool            irb;                /* L3VNI / IRB enabled */
} danos_evpn_evi_t;

/* EVPN EVI CRUD (v0.2). Keyed by evi. */
danos_status_t danos_evpn_evi_create(danos_tx_t *tx, const danos_evpn_evi_t *evi);
danos_status_t danos_evpn_evi_update(danos_tx_t *tx, const danos_evpn_evi_t *evi);
danos_status_t danos_evpn_evi_delete(danos_tx_t *tx, uint32_t evi);
danos_status_t danos_evpn_evi_read(danos_tx_t *tx, uint32_t evi,
                                   danos_evpn_evi_t *out);

/* =========================================================================
 * 15. Object: Multicast (v0.2+; declared here for ABI stability)
 * ========================================================================= */

typedef struct {
    danos_vrf_id_t     vrf_id;
    danos_ip_addr_t    source;          /* S for (S,G); 0 for (*,G) */
    danos_ip_addr_t    group;
    danos_obj_id_t     incoming_if;     /* RPF interface */
    uint32_t           oif_count;
    danos_ifindex_t    oif_list[64];    /* outgoing interface list */
} danos_mroute_t;

/* Multicast mroute CRUD (v0.2). Keyed by composite (vrf_id, group).
 * For (*,G) routes, source.af = DANOS_AF_UNSPEC. */
danos_status_t danos_mroute_create(danos_tx_t *tx, const danos_mroute_t *mr);
danos_status_t danos_mroute_update(danos_tx_t *tx, const danos_mroute_t *mr);
danos_status_t danos_mroute_delete(danos_tx_t *tx, danos_vrf_id_t vrf_id,
                                   const danos_ip_addr_t *group);
danos_status_t danos_mroute_read(danos_tx_t *tx, danos_vrf_id_t vrf_id,
                                 const danos_ip_addr_t *group,
                                 danos_mroute_t *out);

/* =========================================================================
 * 16. Object: BFD Session
 * ========================================================================= */

typedef struct {
    danos_obj_id_t    id;               /* key */
    danos_ip_addr_t   remote;
    danos_ifindex_t   ifindex;          /* 0 for multi-hop */
    uint32_t          desired_tx_ms;
    uint32_t          required_rx_ms;
    uint32_t          detect_mult;      /* 3..50 */
    bool              admin_up;
    bool              session_up;       /* oper, read-only */
} danos_bfd_t;

danos_status_t danos_bfd_create(danos_tx_t *tx, const danos_bfd_t *bfd);
danos_status_t danos_bfd_delete(danos_tx_t *tx, danos_obj_id_t id);

/* =========================================================================
 * 17. Reconciliation API
 * ========================================================================= */

typedef struct {
    uint64_t reconcile_period_ms;   /* default 30000 */
    uint32_t max_retries;           /* default 5 */
    uint64_t backoff_initial_ms;    /* default 1000 */
    uint64_t backoff_max_ms;        /* default 60000 */
    uint32_t antiflap_window_ms;    /* default 5000 */
    uint32_t antiflap_max_count;    /* default 3 */
} danos_reconcile_config_t;

/* Trigger immediate reconciliation for a specific object type. */
danos_status_t danos_reconcile_trigger(danos_obj_type_t type);

/* Get reconciliation stats for monitoring. */
typedef struct {
    uint64_t total_runs;
    uint64_t total_diffs;
    uint64_t total_repairs;
    uint64_t total_failures;
    uint64_t total_flaps;
} danos_reconcile_stats_t;

danos_status_t danos_reconcile_get_stats(danos_obj_type_t type, danos_reconcile_stats_t *out);

/* =========================================================================
 * 18. Event / Notification API (for telemetry & HA)
 * ========================================================================= */

typedef enum {
    DANOS_EVENT_OBJ_CREATED  = 1,
    DANOS_EVENT_OBJ_UPDATED  = 2,
    DANOS_EVENT_OBJ_DELETED  = 3,
    DANOS_EVENT_TX_COMMITTED = 4,
    DANOS_EVENT_TX_ROLLBACK  = 5,
    DANOS_EVENT_RECONCILE    = 6,
    DANOS_EVENT_BACKEND_DOWN = 7,
    DANOS_EVENT_BACKEND_UP   = 8,
    DANOS_EVENT_CAPABILITY   = 9,
} danos_event_type_t;

typedef struct {
    danos_event_type_t type;
    uint64_t           timestamp_ns;
    danos_obj_type_t   obj_type;
    danos_obj_id_t     obj_id;
    danos_tx_id_t      tx_id;
    const char        *message;
} danos_event_t;

typedef void (*danos_event_cb_t)(const danos_event_t *event, void *user);

/* Subscribe to events. Returns subscription handle (0 on error). */
uint64_t danos_event_subscribe(danos_event_type_t mask,
                               danos_event_cb_t cb,
                               void *user);

void danos_event_unsubscribe(uint64_t sub_id);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_DPA_H__ */
