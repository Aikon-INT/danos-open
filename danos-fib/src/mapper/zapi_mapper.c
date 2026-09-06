/*
 * DANOS-Open FIB: ZAPI → DPA Mapper (C2)
 *
 * Maps FRR Zebra ZAPI messages to DPA object operations.
 * This is the core of the FIB Adapter: it translates control-plane
 * events (route add/delete, interface up/down, etc.) into DPA
 * transaction operations.
 */

#include "zapi/zapi.h"
#include <danos/dpa.h>
#include <string.h>
#include <stdio.h>

/* ZAPI route protocol → DPA route protocol */
static danos_route_proto_t map_protocol(uint8_t zapi_proto)
{
    switch (zapi_proto) {
    case 0:  return DANOS_ROUTE_PROTO_KERNEL;
    case 1:  return DANOS_ROUTE_PROTO_STATIC;
    case 2:  return DANOS_ROUTE_PROTO_BGP;
    case 3:  return DANOS_ROUTE_PROTO_OSPF;
    case 4:  return DANOS_ROUTE_PROTO_ISIS;
    case 5:  return DANOS_ROUTE_PROTO_CONNECTED;
    default: return DANOS_ROUTE_PROTO_UNSPEC;
    }
}

/* =========================================================================
 * ZEBRA_ROUTE_ADD / ZEBRA_ROUTE_DELETE → DPA Route
 *
 * ZAPI route payload (simplified for v0.1):
 *   [vrf_id:4][family:1][prefix_len:1][prefix:4or16][protocol:1]
 *   [admin_distance:1][metric:4][nexthop_count:1]
 *   per nexthop: [type:1][gateway:4or16][ifindex:4]
 * ========================================================================= */
danos_status_t zapi_map_route(const zapi_message_t *msg, danos_tx_t *tx,
                              bool is_add)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    uint32_t vrf_id;
    uint8_t  family, prefix_len, proto, admin_dist, nh_count;

    if (zapi_decode_u32(&d, &vrf_id) != 0)     return DANOS_ERR_INVALID_ARG;
    if (zapi_decode_u8(&d, &family) != 0)      return DANOS_ERR_INVALID_ARG;
    if (zapi_decode_u8(&d, &prefix_len) != 0)  return DANOS_ERR_INVALID_ARG;

    danos_route_t route;
    memset(&route, 0, sizeof(route));
    route.vrf_id = vrf_id;
    route.prefix.prefix_len = prefix_len;
    route.prefix.addr.af = (family == 4) ? DANOS_AF_IPV4 : DANOS_AF_IPV6;

    /* Read prefix bytes */
    size_t pfx_bytes = (family == 4) ? 4 : 16;
    if (zapi_decode_bytes(&d, route.prefix.addr.addr, pfx_bytes) != 0)
        return DANOS_ERR_INVALID_ARG;

    if (zapi_decode_u8(&d, &proto) != 0)       return DANOS_ERR_INVALID_ARG;
    if (zapi_decode_u8(&d, &admin_dist) != 0)  return DANOS_ERR_INVALID_ARG;
    route.protocol = map_protocol(proto);
    route.admin_distance = admin_dist;

    uint32_t metric;
    if (zapi_decode_u32(&d, &metric) != 0)     return DANOS_ERR_INVALID_ARG;
    route.metric = metric;

    if (zapi_decode_u8(&d, &nh_count) != 0)    return DANOS_ERR_INVALID_ARG;

    /* For v0.1: single nexthop → create NH + NHGroup */
    if (nh_count > 0 && is_add) {
        uint8_t nh_type;
        if (zapi_decode_u8(&d, &nh_type) != 0) return DANOS_ERR_INVALID_ARG;

        danos_nexthop_t nh;
        memset(&nh, 0, sizeof(nh));
        nh.id = 1;  /* simplified */
        nh.gateway.af = route.prefix.addr.af;
        size_t gw_bytes = (family == 4) ? 4 : 16;
        if (zapi_decode_bytes(&d, nh.gateway.addr, gw_bytes) != 0)
            return DANOS_ERR_INVALID_ARG;

        uint32_t ifindex;
        if (zapi_decode_u32(&d, &ifindex) != 0) return DANOS_ERR_INVALID_ARG;
        nh.ifindex = ifindex;
        nh.weight = 1;

        danos_nh_create(tx, &nh);

        danos_nhgroup_t grp;
        memset(&grp, 0, sizeof(grp));
        grp.id = 1;
        grp.nh_count = 1;
        grp.nh_ids[0] = nh.id;
        danos_nhgroup_create(tx, &grp);

        route.nhgroup_id = grp.id;
    }

    if (is_add) {
        return danos_route_create(tx, &route);
    } else {
        return danos_route_delete(tx, vrf_id, route.prefix, route.protocol);
    }
}

/* =========================================================================
 * ZEBRA_INTERFACE_ADD / ZEBRA_INTERFACE_DELETE → DPA Interface
 *
 * ZAPI interface payload (simplified):
 *   [ifindex:4][name_len:1][name:variable][mtu:4][mac:6]
 * ========================================================================= */
danos_status_t zapi_map_interface(const zapi_message_t *msg, danos_tx_t *tx,
                                  bool is_add)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    uint32_t ifindex;
    uint8_t  name_len;

    if (zapi_decode_u32(&d, &ifindex) != 0)   return DANOS_ERR_INVALID_ARG;
    if (zapi_decode_u8(&d, &name_len) != 0)   return DANOS_ERR_INVALID_ARG;
    if (name_len >= 64) name_len = 63;

    danos_iface_t iface;
    memset(&iface, 0, sizeof(iface));
    iface.ifindex = ifindex;
    iface.type = DANOS_IF_TYPE_PHYS;
    iface.mtu = 1500;
    iface.admin_up = true;

    if (name_len > 0) {
        if (zapi_decode_bytes(&d, (uint8_t *)iface.name, name_len) != 0)
            return DANOS_ERR_INVALID_ARG;
        iface.name[name_len] = '\0';
    }

    /* MTU (optional) */
    uint32_t mtu;
    if (zapi_decode_u32(&d, &mtu) == 0) {
        iface.mtu = (uint16_t)mtu;
    }

    /* MAC (optional) */
    zapi_decode_bytes(&d, iface.mac, 6);

    if (is_add) {
        return danos_iface_create(tx, &iface);
    } else {
        return danos_iface_delete(tx, ifindex);
    }
}

/* =========================================================================
 * ZEBRA_BFD_DEST_REGISTER → DPA BFD Session
 * ========================================================================= */
danos_status_t zapi_map_bfd(const zapi_message_t *msg, danos_tx_t *tx,
                            bool is_add)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    danos_bfd_t bfd;
    memset(&bfd, 0, sizeof(bfd));
    bfd.id = 1;
    bfd.admin_up = is_add;

    uint8_t family;
    if (zapi_decode_u8(&d, &family) != 0) return DANOS_ERR_INVALID_ARG;
    bfd.remote.af = (family == 4) ? DANOS_AF_IPV4 : DANOS_AF_IPV6;

    size_t addr_bytes = (family == 4) ? 4 : 16;
    if (zapi_decode_bytes(&d, bfd.remote.addr, addr_bytes) != 0)
        return DANOS_ERR_INVALID_ARG;

    uint32_t ifindex;
    if (zapi_decode_u32(&d, &ifindex) == 0) {
        bfd.ifindex = ifindex;
    }

    uint32_t tx_ms, rx_ms, detect_mult;
    if (zapi_decode_u32(&d, &tx_ms) == 0) bfd.desired_tx_ms = tx_ms;
    if (zapi_decode_u32(&d, &rx_ms) == 0) bfd.required_rx_ms = rx_ms;
    if (zapi_decode_u32(&d, &detect_mult) == 0) bfd.detect_mult = detect_mult;

    if (is_add) {
        return danos_bfd_create(tx, &bfd);
    } else {
        return danos_bfd_delete(tx, bfd.id);
    }
}

/* =========================================================================
 * ZEBRA_INTERFACE_SET_MTU → DPA Interface update (MTU field)
 *
 * ZAPI payload: [ifindex:4][mtu:4]
 *
 * Reads existing interface, updates MTU, writes back. If interface does not
 * exist, returns NOT_FOUND (FRR should not send SET_MTU before INTERFACE_ADD).
 * ========================================================================= */
danos_status_t zapi_map_interface_set_mtu(const zapi_message_t *msg,
                                          danos_tx_t *tx)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    uint32_t ifindex, mtu;
    if (zapi_decode_u32(&d, &ifindex) != 0) return DANOS_ERR_INVALID_ARG;
    if (zapi_decode_u32(&d, &mtu) != 0)     return DANOS_ERR_INVALID_ARG;

    /* Read existing interface, update MTU, write back */
    danos_iface_t iface;
    danos_status_t st = danos_iface_read(tx, ifindex, &iface);
    if (st != DANOS_OK) return st;
    iface.mtu = (uint16_t)mtu;
    return danos_iface_update(tx, &iface);
}

/* =========================================================================
 * ZEBRA_INTERFACE_UP / ZEBRA_INTERFACE_DOWN → DPA Interface update (admin_up)
 *
 * ZAPI payload: [ifindex:4]
 * ========================================================================= */
danos_status_t zapi_map_interface_set_admin(const zapi_message_t *msg,
                                            danos_tx_t *tx,
                                            bool admin_up)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    uint32_t ifindex;
    if (zapi_decode_u32(&d, &ifindex) != 0) return DANOS_ERR_INVALID_ARG;

    /* Read existing interface, update admin_up, write back */
    danos_iface_t iface;
    danos_status_t st = danos_iface_read(tx, ifindex, &iface);
    if (st != DANOS_OK) return st;
    iface.admin_up = admin_up;
    return danos_iface_update(tx, &iface);
}

/* =========================================================================
 * ZEBRA_NEXTHOP_LOOKUP → DPA NH read
 *
 * ZAPI payload: [vrf_id:4][family:1][gateway:4or16]
 *
 * v0.1 limitation: DPA NH objects are identified by backend-allocated obj_id,
 * not by (gateway, ifindex) tuple. This mapper performs a best-effort lookup
 * by reading NH id=1 (the simplified single-NH model from ROUTE_ADD).
 * A full implementation requires an NH index by (vrf, gateway), planned v0.2.
 * Here we return the NH if it exists, NOT_FOUND otherwise.
 * ========================================================================= */
danos_status_t zapi_map_nexthop_lookup(const zapi_message_t *msg,
                                       danos_tx_t *tx,
                                       danos_nexthop_t *out_nh)
{
    if (!msg || !tx || !out_nh) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    uint32_t vrf_id;
    uint8_t  family;
    if (zapi_decode_u32(&d, &vrf_id) != 0)  return DANOS_ERR_INVALID_ARG;
    if (zapi_decode_u8(&d, &family) != 0)   return DANOS_ERR_INVALID_ARG;

    /* Read gateway bytes (validated for family but not used in v0.1 lookup) */
    uint8_t gateway[16];
    size_t gw_bytes = (family == 4) ? 4 : 16;
    if (zapi_decode_bytes(&d, gateway, gw_bytes) != 0)
        return DANOS_ERR_INVALID_ARG;

    /* v0.1 simplified: NH id=1 is the single NH created by ROUTE_ADD.
     * Full (vrf, gateway) index is a v0.2 enhancement. */
    (void)vrf_id;
    return danos_nh_read(tx, 1, out_nh);
}

/* =========================================================================
 * ZEBRA_REDISTRIBUTE_ADD → no-op (handled by FRR via subsequent ROUTE_ADD)
 *
 * ZAPI payload: [protocol:1]
 *
 * FRR sends REDISTRIBUTE_ADD to signal that a protocol's routes should be
 * redistributed into zebra. The actual route updates arrive as subsequent
 * ZEBRA_ROUTE_ADD messages, which zapi_map_route handles. This mapper
 * acknowledges the command without performing a DPA operation.
 * ========================================================================= */
danos_status_t zapi_map_redistribute_add(const zapi_message_t *msg,
                                         danos_tx_t *tx)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);

    uint8_t protocol;
    if (zapi_decode_u8(&d, &protocol) != 0) return DANOS_ERR_INVALID_ARG;

    /* No DPA operation: FRR will follow up with ZEBRA_ROUTE_ADD messages.
     * Acknowledge by returning OK. */
    return DANOS_OK;
}

/* =========================================================================
 * Dispatch: route any ZAPI message to the appropriate DPA operation
 * ========================================================================= */
danos_status_t zapi_dispatch(const zapi_message_t *msg, danos_tx_t *tx)
{
    if (!msg || !tx) return DANOS_ERR_INVALID_ARG;

    switch (msg->header.command) {
    case ZEBRA_ROUTE_ADD:
        return zapi_map_route(msg, tx, true);
    case ZEBRA_ROUTE_DELETE:
        return zapi_map_route(msg, tx, false);
    case ZEBRA_REDISTRIBUTE_ADD:
        return zapi_map_redistribute_add(msg, tx);
    case ZEBRA_INTERFACE_ADD:
        return zapi_map_interface(msg, tx, true);
    case ZEBRA_INTERFACE_DELETE:
        return zapi_map_interface(msg, tx, false);
    case ZEBRA_INTERFACE_SET_MTU:
        return zapi_map_interface_set_mtu(msg, tx);
    case ZEBRA_INTERFACE_UP:
        return zapi_map_interface_set_admin(msg, tx, true);
    case ZEBRA_INTERFACE_DOWN:
        return zapi_map_interface_set_admin(msg, tx, false);
    case ZEBRA_NEXTHOP_LOOKUP: {
        danos_nexthop_t nh;
        return zapi_map_nexthop_lookup(msg, tx, &nh);
    }
    case ZEBRA_BFD_DEST_REGISTER:
        return zapi_map_bfd(msg, tx, true);
    case ZEBRA_BFD_DEST_DEREGISTER:
        return zapi_map_bfd(msg, tx, false);
    default:
        /* Unsupported command — log and skip */
        return DANOS_ERR_NOT_SUPPORTED;
    }
}
