/*
 * DANOS-Open VPP Backend: Typed Binary API Messages (v0.3)
 */

#include "vpp_msgs.h"
#include "vpp_wire.h"
#include "vpp_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* fib_path_nh is a union.  The address member is the largest member
 * (16 bytes); via_label, obj_id and classify_table_index overlay it at
 * offset zero and do not extend the wire layout. */
#define FIB_PATH_NH_SIZE 16
/* fib_mpls_label { u8 is_uniform; u32 label; u8 ttl; u8 exp; } packed = 7 */
#define FIB_MPLS_LABEL_SIZE 7
#define FIB_PATH_MAX_LABELS 16

/* fib_path fixed size (see vpp_msgs.h for layout) */
#define FIB_PATH_SIZE (4 + 4 + 4 + 1 + 1 + 4 + 4 + 4 + \
                       FIB_PATH_NH_SIZE + 1 + \
                       FIB_PATH_MAX_LABELS * FIB_MPLS_LABEL_SIZE)

/* fib_path_type_t */
#define FIB_PATH_TYPE_API_NORMAL 0
/* fib_path_nh_proto_t */
#define FIB_PATH_NH_PROTO_API_IP4 0
#define FIB_PATH_NH_PROTO_API_IP6 1

static void encode_address(vpp_buf_t *b, const vpp_ip_t *a)
{
    /* address { u8 af; address_union un; } — union is 16 bytes,
     * zero-padded for IPv4 */
    uint8_t un[16] = {0};
    uint8_t af;
    uint32_t n;
    if (a->is_ipv6) {
        af = 1;  /* ADDRESS_IP6 */
        n = 16;
    } else {
        af = 0;  /* ADDRESS_IP4 */
        n = 4;
    }
    memcpy(un, a->addr, n);
    vpp_buf_put_u8(b, af);
    vpp_buf_put_bytes(b, un, 16);
}

static void encode_prefix(vpp_buf_t *b, const vpp_prefix_t *p)
{
    /* prefix { address address; u8 len; } */
    encode_address(b, &p->addr);
    vpp_buf_put_u8(b, p->len);
}

static void encode_fib_path(vpp_buf_t *b, uint32_t sw_if_index, uint32_t table_id,
                            const vpp_ip_t *nh)
{
    uint8_t nhbuf[16] = {0};
    if (nh) {
        uint32_t n = nh->is_ipv6 ? 16 : 4;
        memcpy(nhbuf, nh->addr, n);
    }
    vpp_buf_put_u32(b, sw_if_index);   /* sw_if_index */
    vpp_buf_put_u32(b, table_id);      /* table_id */
    vpp_buf_put_u32(b, 0);             /* rpf_id */
    vpp_buf_put_u8(b, 1);              /* weight */
    vpp_buf_put_u8(b, 0);              /* preference */
    vpp_buf_put_u32(b, FIB_PATH_TYPE_API_NORMAL);  /* type enum */
    vpp_buf_put_u32(b, 0);              /* flags enum */
    vpp_buf_put_u32(b, nh && nh->is_ipv6 ? FIB_PATH_NH_PROTO_API_IP6
                                         : FIB_PATH_NH_PROTO_API_IP4);
    vpp_buf_put_bytes(b, nhbuf, sizeof(nhbuf));
    vpp_buf_put_u8(b, 0);              /* n_labels */
    vpp_buf_t pad = {0};
    (void)pad;
    /* label_stack[16] zero-filled */
    uint8_t zeros[FIB_PATH_MAX_LABELS * FIB_MPLS_LABEL_SIZE] = {0};
    vpp_buf_put_bytes(b, zeros, sizeof(zeros));
}

/* =========================================================================
 * Encoders (pure wire layout, testable without a socket)
 * ========================================================================= */

int vpp_encode_fib_path(uint8_t *out, uint32_t out_size,
                        uint32_t sw_if_index, uint32_t table_id,
                        const vpp_ip_t *nh)
{
    if (!out || out_size < FIB_PATH_SIZE) return -1;
    vpp_buf_t b;
    vpp_buf_init(&b, FIB_PATH_SIZE);
    encode_fib_path(&b, sw_if_index, table_id, nh);
    uint32_t n = b.len < out_size ? b.len : out_size;
    memcpy(out, b.data, n);
    vpp_buf_free(&b);
    return (int)n;
}

int vpp_encode_ip_route_add_del(uint8_t is_add, uint32_t table_id,
                                const vpp_prefix_t *prefix,
                                uint32_t n_paths, const vpp_ip_t *nhs,
                                const uint32_t *nh_ifs,
                                uint8_t *out, uint32_t out_size)
{
    if (!prefix || !nhs || !nh_ifs || n_paths == 0 || n_paths > 64)
        return -1;

    vpp_buf_t b;
    vpp_buf_init(&b, 128);
    vpp_buf_put_u8(&b, is_add);         /* bool is_add */
    vpp_buf_put_u8(&b, 0);              /* bool is_multipath */
    /* ip_route */
    vpp_buf_put_u32(&b, table_id);      /* table_id */
    vpp_buf_put_u32(&b, 0);             /* stats_index */
    encode_prefix(&b, prefix);
    vpp_buf_put_u8(&b, (uint8_t)n_paths);
    for (uint32_t i = 0; i < n_paths; i++)
        encode_fib_path(&b, nh_ifs[i], table_id, &nhs[i]);

    int n = -1;
    if (b.len <= out_size) {
        memcpy(out, b.data, b.len);
        n = (int)b.len;
    }
    vpp_buf_free(&b);
    return n;
}

int vpp_encode_ip_table_add_del(uint8_t is_add, uint32_t table_id,
                                bool is_ip6, const char *name,
                                uint8_t *out, uint32_t out_size)
{
    vpp_buf_t b;
    vpp_buf_init(&b, 96);
    vpp_buf_put_u8(&b, is_add);
    vpp_buf_put_u32(&b, table_id);
    vpp_buf_put_u8(&b, is_ip6 ? 1 : 0);
    vpp_buf_put_string(&b, name ? name : "");

    int n = -1;
    if (b.len <= out_size) {
        memcpy(out, b.data, b.len);
        n = (int)b.len;
    }
    vpp_buf_free(&b);
    return n;
}

int vpp_encode_sw_interface_set_flags(uint32_t sw_if_index, bool admin_up,
                                      uint8_t *out, uint32_t out_size)
{
    if (!out || out_size < 8) return -1;
    vpp_buf_t b;
    vpp_buf_init(&b, 8);
    vpp_buf_put_u32(&b, sw_if_index);
    vpp_buf_put_u32(&b, admin_up ? 0x1 : 0x0);  /* IF_STATUS_API_FLAG_ADMIN_UP */
    memcpy(out, b.data, b.len);
    int n = (int)b.len;
    vpp_buf_free(&b);
    return n;
}

int vpp_encode_sw_interface_add_del_address(uint32_t sw_if_index, bool is_add,
                                            const vpp_prefix_t *prefix,
                                            uint8_t *out, uint32_t out_size)
{
    if (!prefix || !out || (prefix->addr.is_ipv6 && prefix->len > 128) ||
        (!prefix->addr.is_ipv6 && prefix->len > 32) || out_size < 24)
        return -1;
    vpp_buf_t b;
    vpp_buf_init(&b, 32);
    vpp_buf_put_u32(&b, sw_if_index);
    vpp_buf_put_u8(&b, is_add ? 1 : 0);
    vpp_buf_put_u8(&b, 0); /* del_all */
    encode_address(&b, &prefix->addr);
    vpp_buf_put_u8(&b, prefix->len);
    if (b.len > out_size) { vpp_buf_free(&b); return -1; }
    memcpy(out, b.data, b.len);
    int n = (int)b.len;
    vpp_buf_free(&b);
    return n;
}

int vpp_encode_ip_neighbor_add_del(uint8_t is_add, uint32_t sw_if_index,
                                   const uint8_t mac[6], const vpp_ip_t *ip,
                                   uint8_t *out, uint32_t out_size)
{
    if (!mac || !ip) return -1;
    vpp_buf_t b;
    vpp_buf_init(&b, 64);
    vpp_buf_put_u8(&b, is_add);
    vpp_buf_put_u8(&b, 0);              /* is_del_all */
    /* ip_neighbor */
    vpp_buf_put_u32(&b, sw_if_index);
    vpp_buf_put_u8(&b, 0);              /* flags (static=0) */
    vpp_buf_put_bytes(&b, mac, 6);
    encode_address(&b, ip);

    int n = -1;
    if (b.len <= out_size) {
        memcpy(out, b.data, b.len);
        n = (int)b.len;
    }
    vpp_buf_free(&b);
    return n;
}

/* =========================================================================
 * Transactions
 * ========================================================================= */

static danos_status_t retval_to_status(int32_t retval)
{
    if (retval == 0) return DANOS_OK;
    if (retval == -18 /* VNET_API_ERROR_INVALID_SW_IF_INDEX */ ||
        retval == -18)
        return DANOS_ERR_NOT_FOUND;
    return DANOS_ERR_BACKEND_IO;
}

static danos_status_t transact_named(const char *msg_name,
                                      const uint8_t *payload, uint32_t len)
{
    uint16_t msg_id;
    if (!danos_vpp_api_lookup_msg_id(msg_name, &msg_id)) {
        return DANOS_ERR_NOT_SUPPORTED;
    }
    uint8_t reply[512];
    int n = danos_vpp_api_transact(msg_id, payload, len, reply, sizeof(reply));
    if (n < 8) return DANOS_ERR_BACKEND_IO;
    /* reply: [u16 msg_id][u32 context][i32 retval] */
    vpp_reader_t r;
    vpp_reader_init(&r, reply + 2, (uint32_t)n - 2);
    (void)vpp_rd_u32(&r);            /* context */
    int32_t retval = (int32_t)vpp_rd_u32(&r);
    if (!vpp_reader_ok(&r)) return DANOS_ERR_BACKEND_IO;
    if (retval != 0) {
        const char *debug = getenv("DANOS_VPP_DEBUG");
        if (debug && debug[0] == '1')
            fprintf(stderr, "vpp api %s failed: retval=%d\\n", msg_name, retval);
    }
    return retval_to_status(retval);
}

danos_status_t vpp_msg_sw_interface_set_flags(uint32_t sw_if_index, bool admin_up)
{
    uint8_t body[8];
    int n = vpp_encode_sw_interface_set_flags(sw_if_index, admin_up,
                                              body, sizeof(body));
    if (n < 0) return DANOS_ERR_INVALID_ARG;
    return transact_named("sw_interface_set_flags", body, (uint32_t)n);
}

danos_status_t vpp_msg_sw_interface_add_del_address(uint32_t sw_if_index,
                                                    bool is_add,
                                                    const vpp_prefix_t *prefix)
{
    uint8_t body[32];
    int n = vpp_encode_sw_interface_add_del_address(sw_if_index, is_add,
                                                     prefix, body, sizeof(body));
    if (n < 0) return DANOS_ERR_INVALID_ARG;
    return transact_named("sw_interface_add_del_address", body, (uint32_t)n);
}

danos_status_t vpp_msg_ip_table_add_del(uint32_t table_id, bool is_ip6,
                                        const char *name, bool is_add)
{
    uint8_t body[96];
    int n = vpp_encode_ip_table_add_del(is_add ? 1 : 0, table_id, is_ip6,
                                        name, body, sizeof(body));
    if (n < 0) return DANOS_ERR_INVALID_ARG;
    return transact_named("ip_table_add_del", body, (uint32_t)n);
}

danos_status_t vpp_msg_ip_route_add_del(bool is_add, uint32_t table_id,
                                        const vpp_prefix_t *prefix,
                                        uint32_t n_paths, const vpp_ip_t *nhs,
                                        const uint32_t *nh_ifs)
{
    uint8_t body[64 * FIB_PATH_SIZE + 64];
    int n = vpp_encode_ip_route_add_del(is_add ? 1 : 0, table_id, prefix,
                                        n_paths, nhs, nh_ifs,
                                        body, sizeof(body));
    if (n < 0) return DANOS_ERR_INVALID_ARG;
    return transact_named("ip_route_add_del", body, (uint32_t)n);
}

danos_status_t vpp_msg_ip_neighbor_add_del(bool is_add, uint32_t sw_if_index,
                                           const uint8_t mac[6],
                                           const vpp_ip_t *ip)
{
    uint8_t body[64];
    int n = vpp_encode_ip_neighbor_add_del(is_add ? 1 : 0, sw_if_index,
                                           mac, ip, body, sizeof(body));
    if (n < 0) return DANOS_ERR_INVALID_ARG;
    return transact_named("ip_neighbor_add_del", body, (uint32_t)n);
}

danos_status_t vpp_msg_control_ping(void)
{
    /* control_ping: { client_index; context; } — no body fields */
    return transact_named("control_ping", NULL, 0);
}

/* =========================================================================
 * CoPP policer (v0.4)
 *
 * policer_add_del body (after client_index+context):
 *   bool is_add; string name[64]; u32 cir; u32 eir; u64 cb; u64 eb;
 *   u8 rate_type; u8 round_type; u8 type; bool color_aware;
 *   action {u8 type; u8 dscp;} x3
 * DPA maps: pir==0 -> 1R2C, else 2R3C RFC2698; rates in KBPS.
 */
#define SSE2_QOS_RATE_API_KBPS   0
#define SSE2_QOS_ROUND_API_TO_CLOSEST 0
#define SSE2_QOS_POLICER_TYPE_API_1R2C 0
#define SSE2_QOS_POLICER_TYPE_API_2R3C_RFC_2698 2
#define SSE2_QOS_ACTION_API_DROP 0
#define SSE2_QOS_ACTION_API_TRANSMIT 1
#define SSE2_QOS_ACTION_API_MARK_AND_TRANSMIT 2

static void encode_policer_action(vpp_buf_t *b, uint8_t action, uint8_t dscp)
{
    vpp_buf_put_u8(b, action);
    vpp_buf_put_u8(b, dscp);
}

int vpp_encode_policer_add_del(uint8_t is_add, const char *name,
                               uint64_t cir_kbps, uint64_t eir_kbps,
                               uint64_t cb_bytes, uint64_t eb_bytes,
                               uint8_t conform_action, uint8_t conform_dscp,
                               uint8_t exceed_action, uint8_t exceed_dscp,
                               uint8_t violate_action, uint8_t violate_dscp,
                               uint8_t *out, uint32_t out_size)
{
    if (!name || !name[0]) return -1;
    vpp_buf_t b;
    vpp_buf_init(&b, 128);
    vpp_buf_put_u8(&b, is_add);
    vpp_buf_put_string(&b, name);
    vpp_buf_put_u32(&b, (uint32_t)cir_kbps);
    vpp_buf_put_u32(&b, (uint32_t)eir_kbps);
    vpp_buf_put_u64(&b, cb_bytes);
    vpp_buf_put_u64(&b, eb_bytes);
    vpp_buf_put_u8(&b, SSE2_QOS_RATE_API_KBPS);
    vpp_buf_put_u8(&b, SSE2_QOS_ROUND_API_TO_CLOSEST);
    vpp_buf_put_u8(&b, eir_kbps > 0 ? SSE2_QOS_POLICER_TYPE_API_2R3C_RFC_2698
                                    : SSE2_QOS_POLICER_TYPE_API_1R2C);
    vpp_buf_put_u8(&b, 0);  /* color_aware */
    encode_policer_action(&b, conform_action, conform_dscp);
    encode_policer_action(&b, exceed_action, exceed_dscp);
    encode_policer_action(&b, violate_action, violate_dscp);

    int n = -1;
    if (b.len <= out_size) {
        memcpy(out, b.data, b.len);
        n = (int)b.len;
    }
    vpp_buf_free(&b);
    return n;
}

danos_status_t vpp_msg_policer_add_del(bool is_add, const char *name,
                                       const danos_qos_policy_t *p)
{
    if (!p) return DANOS_ERR_INVALID_ARG;
    /* bps -> kbps (VPP rate_type=KBPS); round up */
    uint64_t cir_kbps = (p->cir_bps + 999) / 1000;
    uint64_t eir_kbps = (p->pir_bps + 999) / 1000;
    uint8_t body[128];
    int n = vpp_encode_policer_add_del(is_add ? 1 : 0, name,
                                       cir_kbps, eir_kbps,
                                       p->cb_bytes, p->pb_bytes,
                                       SSE2_QOS_ACTION_API_MARK_AND_TRANSMIT,
                                       p->conform_dscp,
                                       eir_kbps ? SSE2_QOS_ACTION_API_MARK_AND_TRANSMIT
                                                : SSE2_QOS_ACTION_API_TRANSMIT,
                                       p->exceed_dscp,
                                       SSE2_QOS_ACTION_API_DROP,
                                       p->violate_dscp,
                                       body, sizeof(body));
    if (n < 0) return DANOS_ERR_INVALID_ARG;
    return transact_named("policer_add_del", body, (uint32_t)n);
}
