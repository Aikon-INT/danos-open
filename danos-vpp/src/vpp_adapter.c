/*
 * DANOS-Open Backend: VPP adapter for the programming pipeline
 * (v0.11, ADR-0007).
 *
 * Implements danos_backend_ops_t over the existing VPP binary-API
 * message layer (vpp_msgs). Registers itself so the reconciler can
 * drive VPP as a second backend alongside netlink.
 *
 * Proof point: the same pipeline that programs the kernel FIB (K4)
 * can drive VPP with only an adapter — no pipeline changes.
 */

#include <danos/core/backend_ops.h>
#include <danos/dpa.h>
#include "api/vpp_api.h"
#include "api/vpp_msgs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t vpp_adapter_map_ifindex(uint32_t ifindex);

static danos_status_t vpp_adapter_iface_up(danos_ifindex_t ifindex,
                                           bool up, void *user)
{
    (void)user;
    return vpp_msg_sw_interface_set_flags(vpp_adapter_map_ifindex(ifindex), up);
}

/* FRR/Linux ifindexes are namespace-local and must not be assumed to equal
 * VPP sw_if_index values.  The integration topology supplies a compact,
 * explicit mapping such as DANOS_VPP_IFINDEX_MAP=2:1,3:2. */
static uint32_t vpp_adapter_map_ifindex(uint32_t ifindex)
{
    const char *map = getenv("DANOS_VPP_IFINDEX_MAP");
    if (!map || !*map) return ifindex;
    const char *p = map;
    while (*p) {
        char *end = NULL;
        unsigned long from = strtoul(p, &end, 10);
        if (end == p || *end != ':') break;
        p = end + 1;
        unsigned long to = strtoul(p, &end, 10);
        if (end == p) break;
        if (from == ifindex && to <= UINT32_MAX) return (uint32_t)to;
        p = (*end == ',') ? end + 1 : end;
    }
    return ifindex;
}

static danos_status_t vpp_adapter_iface_addr_one(const danos_iface_t *iface,
                                                 const danos_ip_prefix_t *p,
                                                 bool is_add)
{
    vpp_prefix_t prefix;
    memset(&prefix, 0, sizeof(prefix));
    prefix.addr.is_ipv6 = p->addr.af == DANOS_AF_IPV6;
    memcpy(prefix.addr.addr, p->addr.addr, prefix.addr.is_ipv6 ? 16 : 4);
    prefix.len = p->prefix_len;
    return vpp_msg_sw_interface_add_del_address(vpp_adapter_map_ifindex(iface->ifindex), is_add, &prefix);
}

static danos_status_t vpp_adapter_iface_addr_set(const danos_iface_t *iface,
                                                 void *user)
{
    (void)user;
    if (iface->ipv4_address.addr.af != DANOS_AF_UNSPEC) {
        danos_status_t st = vpp_adapter_iface_addr_one(iface, &iface->ipv4_address, true);
        if (st != DANOS_OK) return st;
    }
    if (iface->ipv6_address.addr.af != DANOS_AF_UNSPEC)
        return vpp_adapter_iface_addr_one(iface, &iface->ipv6_address, true);
    return DANOS_OK;
}

static danos_status_t vpp_adapter_iface_addr_del(const danos_iface_t *iface,
                                                 void *user)
{
    (void)user;
    if (iface->ipv4_address.addr.af != DANOS_AF_UNSPEC) {
        danos_status_t st = vpp_adapter_iface_addr_one(iface, &iface->ipv4_address, false);
        if (st != DANOS_OK) return st;
    }
    if (iface->ipv6_address.addr.af != DANOS_AF_UNSPEC)
        return vpp_adapter_iface_addr_one(iface, &iface->ipv6_address, false);
    return DANOS_OK;
}

static danos_status_t vpp_adapter_route_add(const danos_resolved_route_t *res,
                                            void *user)
{
    (void)user;
    const danos_route_t *r = &res->route;
    if (r->prefix.addr.af != DANOS_AF_IPV4) return DANOS_ERR_NOT_SUPPORTED;

    vpp_prefix_t prefix;
    memset(&prefix, 0, sizeof(prefix));
    prefix.addr.is_ipv6 = false;
    memcpy(prefix.addr.addr, r->prefix.addr.addr, 4);
    prefix.len = r->prefix.prefix_len;

    /* paths: gateway route when resolved, else blackhole as a local */
    vpp_ip_t nh;
    memset(&nh, 0, sizeof(nh));
    nh.is_ipv6 = false;
    memcpy(nh.addr, res->gw, 4);
    uint32_t nh_if = res->oif ? vpp_adapter_map_ifindex(res->oif) : 1;

    const char *debug = getenv("DANOS_VPP_DEBUG");
    if (debug && debug[0] == '1')
        fprintf(stderr, "vpp route add vrf=%u prefix-len=%u gw=%u.%u.%u.%u if=%u\n",
                r->vrf_id, prefix.len, nh.addr[0], nh.addr[1], nh.addr[2],
                nh.addr[3], nh_if);

    return vpp_msg_ip_route_add_del(true, r->vrf_id, &prefix,
                                    1, &nh, &nh_if);
}

static danos_status_t vpp_adapter_route_del(const danos_resolved_route_t *res,
                                            void *user)
{
    (void)user;
    const danos_route_t *r = &res->route;
    if (r->prefix.addr.af != DANOS_AF_IPV4) return DANOS_ERR_NOT_SUPPORTED;

    vpp_prefix_t prefix;
    memset(&prefix, 0, sizeof(prefix));
    prefix.addr.is_ipv6 = false;
    memcpy(prefix.addr.addr, r->prefix.addr.addr, 4);
    prefix.len = r->prefix.prefix_len;

    vpp_ip_t nh;
    memset(&nh, 0, sizeof(nh));
    uint32_t nh_if = 1;
    return vpp_msg_ip_route_add_del(false, r->vrf_id, &prefix,
                                    1, &nh, &nh_if);
}

static danos_status_t vpp_adapter_vrf_add(const danos_vrf_t *vrf, void *user)
{
    (void)user;
    return vpp_msg_ip_table_add_del(vrf->vrf_id, false, vrf->name, true);
}

static danos_status_t vpp_adapter_vrf_del(danos_vrf_id_t vrf_id, void *user)
{
    (void)user;
    return vpp_msg_ip_table_add_del(vrf_id, false, NULL, false);
}

static danos_backend_ops_t g_vpp_ops = {
    .name      = "vpp",
    .iface_up  = vpp_adapter_iface_up,
    .iface_addr_set = vpp_adapter_iface_addr_set,
    .iface_addr_del = vpp_adapter_iface_addr_del,
    .route_add = vpp_adapter_route_add,
    .route_del = vpp_adapter_route_del,
    .vrf_add   = vpp_adapter_vrf_add,
    .vrf_del   = vpp_adapter_vrf_del,
    .user      = NULL,
};

/* Connect the VPP binary API (mock or real) and install the ops. */
int danos_vpp_adapter_install(bool real, const char *sock_path)
{
    danos_vpp_api_init();
    if (!real) {
        danos_vpp_api_enable_mock();
    } else {
        if (sock_path) danos_vpp_api_set_sock_path(sock_path);
        if (danos_vpp_api_connect() != 0) return -1;
    }
    danos_backend_ops_set(&g_vpp_ops);
    return 0;
}
