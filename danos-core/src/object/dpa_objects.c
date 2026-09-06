/*
 * DANOS-Open Core: DPA Object CRUD API implementation
 *
 * Implements the public DPA object functions from danos/dpa.h.
 * For v0.1, these operate on the in-memory object store.
 * In production, they dispatch to the appropriate backend.
 */

#include <danos/dpa.h>
#include <danos/core/object_registry.h>
#include <danos/core/transaction.h>
#include <string.h>
#include <stdlib.h>

/* Lazy-init default store */
static danos_object_store_t *get_default_store(void)
{
    if (!g_default_store) {
        g_default_store = danos_object_store_create(1024);
    }
    return g_default_store;
}

/* =========================================================================
 * Interface CRUD
 * ========================================================================= */
danos_status_t danos_iface_create(danos_tx_t *tx, const danos_iface_t *iface)
{
    (void)tx;
    if (!iface) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_IFACE,
                               iface->ifindex, iface, sizeof(*iface));
}

danos_status_t danos_iface_update(danos_tx_t *tx, const danos_iface_t *iface)
{
    (void)tx;
    if (!iface) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_IFACE,
                               iface->ifindex, iface, sizeof(*iface));
}

danos_status_t danos_iface_delete(danos_tx_t *tx, danos_ifindex_t ifindex)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_IFACE, ifindex);
}

danos_status_t danos_iface_read(danos_tx_t *tx, danos_ifindex_t ifindex, danos_iface_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_IFACE, ifindex, out, &sz);
}

/* =========================================================================
 * VRF CRUD
 * ========================================================================= */
danos_status_t danos_vrf_create(danos_tx_t *tx, const danos_vrf_t *vrf)
{
    (void)tx;
    if (!vrf) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_VRF,
                               vrf->vrf_id, vrf, sizeof(*vrf));
}

danos_status_t danos_vrf_update(danos_tx_t *tx, const danos_vrf_t *vrf)
{
    (void)tx;
    if (!vrf) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_VRF,
                               vrf->vrf_id, vrf, sizeof(*vrf));
}

danos_status_t danos_vrf_delete(danos_tx_t *tx, danos_vrf_id_t vrf_id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_VRF, vrf_id);
}

danos_status_t danos_vrf_read(danos_tx_t *tx, danos_vrf_id_t vrf_id, danos_vrf_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_VRF, vrf_id, out, &sz);
}

/* =========================================================================
 * NextHop CRUD
 * ========================================================================= */
danos_status_t danos_nh_create(danos_tx_t *tx, const danos_nexthop_t *nh)
{
    (void)tx;
    if (!nh) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_NEXTHOP,
                               nh->id, nh, sizeof(*nh));
}

danos_status_t danos_nh_update(danos_tx_t *tx, const danos_nexthop_t *nh)
{
    (void)tx;
    if (!nh) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_NEXTHOP,
                               nh->id, nh, sizeof(*nh));
}

danos_status_t danos_nh_delete(danos_tx_t *tx, danos_obj_id_t id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_NEXTHOP, id);
}

danos_status_t danos_nh_read(danos_tx_t *tx, danos_obj_id_t id, danos_nexthop_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_NEXTHOP, id, out, &sz);
}

/* =========================================================================
 * NHGroup CRUD
 * ========================================================================= */
danos_status_t danos_nhgroup_create(danos_tx_t *tx, const danos_nhgroup_t *grp)
{
    (void)tx;
    if (!grp) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_NHGROUP,
                               grp->id, grp, sizeof(*grp));
}

danos_status_t danos_nhgroup_update(danos_tx_t *tx, const danos_nhgroup_t *grp)
{
    (void)tx;
    if (!grp) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_NHGROUP,
                               grp->id, grp, sizeof(*grp));
}

danos_status_t danos_nhgroup_delete(danos_tx_t *tx, danos_obj_id_t id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_NHGROUP, id);
}

danos_status_t danos_nhgroup_read(danos_tx_t *tx, danos_obj_id_t id, danos_nhgroup_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_NHGROUP, id, out, &sz);
}

/* =========================================================================
 * Route CRUD
 * ========================================================================= */
/* Route key = (vrf_id, prefix, protocol) → hash to obj_id */
static danos_obj_id_t route_key(danos_vrf_id_t vrf, const danos_ip_prefix_t *p,
                                danos_route_proto_t proto)
{
    /* FNV-1a hash: good distribution for IP prefixes */
    uint64_t h = 14695981039346656037ULL;  /* FNV offset basis */
    h ^= (uint64_t)vrf;
    h *= 1099511628211ULL;  /* FNV prime */
    for (int i = 0; i < 16; i++) {
        h ^= p->addr.addr[i];
        h *= 1099511628211ULL;
    }
    h ^= p->prefix_len;
    h *= 1099511628211ULL;
    h ^= (uint64_t)proto;
    h *= 1099511628211ULL;
    return h;
}

danos_status_t danos_route_create(danos_tx_t *tx, const danos_route_t *route)
{
    (void)tx;
    if (!route) return DANOS_ERR_INVALID_ARG;
    danos_obj_id_t id = route_key(route->vrf_id, &route->prefix, route->protocol);
    return danos_object_create(get_default_store(), DANOS_OBJ_ROUTE,
                               id, route, sizeof(*route));
}

danos_status_t danos_route_update(danos_tx_t *tx, const danos_route_t *route)
{
    (void)tx;
    if (!route) return DANOS_ERR_INVALID_ARG;
    danos_obj_id_t id = route_key(route->vrf_id, &route->prefix, route->protocol);
    return danos_object_update(get_default_store(), DANOS_OBJ_ROUTE,
                               id, route, sizeof(*route));
}

danos_status_t danos_route_delete(danos_tx_t *tx, danos_vrf_id_t vrf_id,
                                  danos_ip_prefix_t prefix,
                                  danos_route_proto_t proto)
{
    (void)tx;
    danos_obj_id_t id = route_key(vrf_id, &prefix, proto);
    return danos_object_delete(get_default_store(), DANOS_OBJ_ROUTE, id);
}

danos_status_t danos_route_read(danos_tx_t *tx, danos_vrf_id_t vrf_id,
                                danos_ip_prefix_t prefix,
                                danos_route_proto_t proto,
                                danos_route_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    danos_obj_id_t id = route_key(vrf_id, &prefix, proto);
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_ROUTE, id, out, &sz);
}

danos_status_t danos_route_dump(danos_tx_t *tx, danos_vrf_id_t vrf_id,
                                danos_route_cb_t cb, void *user)
{
    (void)tx; (void)vrf_id; (void)cb; (void)user;
    /* TODO: iterate store and call cb per route */
    return DANOS_OK;
}

/* =========================================================================
 * ACL CRUD
 * ========================================================================= */
danos_status_t danos_acl_table_create(danos_tx_t *tx, const danos_acl_table_t *tbl)
{
    (void)tx;
    if (!tbl) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_ACL,
                               tbl->table_id, tbl, sizeof(*tbl));
}

danos_status_t danos_acl_table_delete(danos_tx_t *tx, danos_obj_id_t table_id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_ACL, table_id);
}

danos_status_t danos_acl_rule_add(danos_tx_t *tx, danos_obj_id_t table_id,
                                  const danos_acl_rule_t *rule)
{
    (void)tx; (void)table_id;
    if (!rule) return DANOS_ERR_INVALID_ARG;
    /* Store rule with rule_id as key (offset to avoid clash with table) */
    return danos_object_create(get_default_store(), DANOS_OBJ_ACL,
                               rule->rule_id + 1000000, rule, sizeof(*rule));
}

danos_status_t danos_acl_rule_delete(danos_tx_t *tx, danos_obj_id_t table_id,
                                     danos_obj_id_t rule_id)
{
    (void)tx; (void)table_id;
    return danos_object_delete(get_default_store(), DANOS_OBJ_ACL, rule_id + 1000000);
}

/* =========================================================================
 * QoS CRUD
 * ========================================================================= */
danos_status_t danos_qos_policy_create(danos_tx_t *tx, const danos_qos_policy_t *p)
{
    (void)tx;
    if (!p) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_QOS,
                               p->policy_id, p, sizeof(*p));
}

danos_status_t danos_qos_policy_delete(danos_tx_t *tx, danos_obj_id_t policy_id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_QOS, policy_id);
}

danos_status_t danos_qos_policy_bind(danos_tx_t *tx, danos_obj_id_t policy_id,
                                     danos_ifindex_t ifindex, bool ingress)
{
    (void)tx; (void)policy_id; (void)ifindex; (void)ingress;
    /* TODO: store binding */
    return DANOS_OK;
}

/* =========================================================================
 * BFD CRUD
 * ========================================================================= */
danos_status_t danos_bfd_create(danos_tx_t *tx, const danos_bfd_t *bfd)
{
    (void)tx;
    if (!bfd) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_BFD,
                               bfd->id, bfd, sizeof(*bfd));
}

danos_status_t danos_bfd_delete(danos_tx_t *tx, danos_obj_id_t id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_BFD, id);
}

/* =========================================================================
 * MPLS LSP CRUD (v0.2)
 * ========================================================================= */
danos_status_t danos_mpls_lsp_create(danos_tx_t *tx, const danos_mpls_lsp_t *lsp)
{
    (void)tx;
    if (!lsp) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_MPLS_LSP,
                               lsp->in_label, lsp, sizeof(*lsp));
}

danos_status_t danos_mpls_lsp_update(danos_tx_t *tx, const danos_mpls_lsp_t *lsp)
{
    (void)tx;
    if (!lsp) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_MPLS_LSP,
                               lsp->in_label, lsp, sizeof(*lsp));
}

danos_status_t danos_mpls_lsp_delete(danos_tx_t *tx, danos_mpls_label_t in_label)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_MPLS_LSP, in_label);
}

danos_status_t danos_mpls_lsp_read(danos_tx_t *tx, danos_mpls_label_t in_label,
                                   danos_mpls_lsp_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_MPLS_LSP,
                             in_label, out, &sz);
}

/* =========================================================================
 * Tunnel CRUD (v0.2)
 * ========================================================================= */
danos_status_t danos_tunnel_create(danos_tx_t *tx, const danos_tunnel_t *tun)
{
    (void)tx;
    if (!tun) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_TUNNEL,
                               tun->id, tun, sizeof(*tun));
}

danos_status_t danos_tunnel_update(danos_tx_t *tx, const danos_tunnel_t *tun)
{
    (void)tx;
    if (!tun) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_TUNNEL,
                               tun->id, tun, sizeof(*tun));
}

danos_status_t danos_tunnel_delete(danos_tx_t *tx, danos_obj_id_t id)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_TUNNEL, id);
}

danos_status_t danos_tunnel_read(danos_tx_t *tx, danos_obj_id_t id,
                                 danos_tunnel_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_TUNNEL,
                             id, out, &sz);
}

/* =========================================================================
 * EVPN EVI CRUD (v0.2)
 * ========================================================================= */
danos_status_t danos_evpn_evi_create(danos_tx_t *tx, const danos_evpn_evi_t *evi)
{
    (void)tx;
    if (!evi) return DANOS_ERR_INVALID_ARG;
    return danos_object_create(get_default_store(), DANOS_OBJ_EVPN,
                               evi->evi, evi, sizeof(*evi));
}

danos_status_t danos_evpn_evi_update(danos_tx_t *tx, const danos_evpn_evi_t *evi)
{
    (void)tx;
    if (!evi) return DANOS_ERR_INVALID_ARG;
    return danos_object_update(get_default_store(), DANOS_OBJ_EVPN,
                               evi->evi, evi, sizeof(*evi));
}

danos_status_t danos_evpn_evi_delete(danos_tx_t *tx, uint32_t evi)
{
    (void)tx;
    return danos_object_delete(get_default_store(), DANOS_OBJ_EVPN, evi);
}

danos_status_t danos_evpn_evi_read(danos_tx_t *tx, uint32_t evi,
                                   danos_evpn_evi_t *out)
{
    (void)tx;
    if (!out) return DANOS_ERR_INVALID_ARG;
    size_t sz = sizeof(*out);
    return danos_object_read(get_default_store(), DANOS_OBJ_EVPN,
                             evi, out, &sz);
}
