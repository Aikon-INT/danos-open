/*
 * DANOS-Open FIB: ZAPI Parser Implementation (C1)
 *
 * Clean-room implementation of FRR Zebra ZAPI wire protocol parsing.
 * Based on public ZAPI protocol documentation.
 */

#include "zapi.h"
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define FRR_ZAPI_HEADER_SIZE 10
#define FRR_ZAPI_MARKER 254

/* =========================================================================
 * Message parse / serialize
 * ========================================================================= */

int zapi_parse(const uint8_t *buf, size_t buf_size, zapi_message_t *out)
{
    if (!buf || !out) return -1;
    if (buf_size < ZAPI_HEADER_SIZE) return -2;

    /* Read length (network byte order) */
    uint32_t length;
    memcpy(&length, buf, 4);
    length = ntohl(length);
    if (length < ZAPI_HEADER_SIZE) return -3;
    if (length > buf_size) return -4;  /* incomplete message */

    /* Read marker */
    uint16_t marker;
    memcpy(&marker, buf + 4, 2);
    marker = ntohs(marker);
    if (marker != ZAPI_HEADER_MARKER) return -5;

    /* Read version */
    uint8_t version = buf[6];
    if (version != ZAPI_VERSION) return -6;

    /* Read command */
    uint16_t command;
    memcpy(&command, buf + 7, 2);
    command = ntohs(command);

    out->header.length  = length;
    out->header.marker  = marker;
    out->header.version = version;
    out->header.command = command;
    out->payload        = buf + ZAPI_HEADER_SIZE;
    out->payload_size   = length - ZAPI_HEADER_SIZE;
    out->vrf_id         = 0;
    return 0;
}

int zapi_parse_frr(const uint8_t *buf, size_t buf_size, zapi_message_t *out)
{
    if (!buf || !out || buf_size < FRR_ZAPI_HEADER_SIZE) return -1;
    uint16_t length, command;
    memcpy(&length, buf, 2);
    length = ntohs(length);
    if (length < FRR_ZAPI_HEADER_SIZE || length > buf_size) return -2;
    if (buf[2] != FRR_ZAPI_MARKER || buf[3] != ZAPI_VERSION) return -3;
    memcpy(&command, buf + 8, 2);
    out->header.length = length;
    out->header.marker = buf[2];
    out->header.version = buf[3];
    out->header.command = ntohs(command);
    memcpy(&out->vrf_id, buf + 4, 4);
    out->vrf_id = ntohl(out->vrf_id);
    out->payload = buf + FRR_ZAPI_HEADER_SIZE;
    out->payload_size = length - FRR_ZAPI_HEADER_SIZE;
    return 0;
}

int zapi_decode_frr_route(const zapi_message_t *msg, zapi_frr_route_t *out)
{
    if (!msg || !out || msg->header.version != ZAPI_VERSION) return -1;
    zapi_decoder_t d;
    zapi_decoder_init(&d, msg->payload, msg->payload_size);
    memset(out, 0, sizeof(*out));
    if (zapi_decode_u8(&d, &out->type) != 0 ||
        zapi_decode_u16(&d, &out->instance) != 0 ||
        zapi_decode_u32(&d, &out->flags) != 0 ||
        zapi_decode_u32(&d, &out->message) != 0 ||
        zapi_decode_u8(&d, &out->safi) != 0 ||
        zapi_decode_u8(&d, &out->family) != 0 ||
        zapi_decode_u8(&d, &out->prefix_len) != 0)
        return -2;
    size_t bytes = out->family == AF_INET6 ? 16 : out->family == AF_INET ? 4 : 0;
    if (!bytes || (out->family == AF_INET && out->prefix_len > 32) ||
        (out->family == AF_INET6 && out->prefix_len > 128) ||
        zapi_decode_bytes(&d, out->prefix, (out->prefix_len + 7) / 8) != 0)
        return -3;
    if (out->message & ZAPI_FRR_MESSAGE_NHG) {
        if (zapi_decode_u32(&d, &out->nhg_id) != 0) return -4;
    }
    if (out->message & ZAPI_FRR_MESSAGE_NEXTHOP) {
        if (zapi_decode_u16(&d, &out->nexthop_count) != 0 ||
            out->nexthop_count > ZAPI_FRR_MAX_NEXTHOPS) return -5;
        for (uint16_t i = 0; i < out->nexthop_count; i++) {
            zapi_frr_nexthop_t *nh = &out->nexthops[i];
            uint32_t nh_vrf;
            if (zapi_decode_u32(&d, &nh_vrf) != 0 ||
                zapi_decode_u8(&d, &nh->type) != 0 ||
                zapi_decode_u8(&d, &nh->flags) != 0) return -6;
            nh->family = out->family;
            /* FRR 10.3 uses type 1 for an ifindex-only nexthop.  The
             * previous decoder treated it as an IPv4 gateway and consumed
             * the ifindex as a gateway, producing values such as 0.0.0.2.
             * Type 3 is the IPv4+ifindex form. */
            if (nh->type == 1) {
                if (zapi_decode_u32(&d, &nh->ifindex) != 0) return -8;
            } else if (nh->type == 2 || nh->type == 3) {
                size_t gw = out->family == AF_INET ? 4 : 16;
                if (zapi_decode_bytes(&d, nh->gateway, gw) != 0) return -7;
                nh->has_gateway = true;
                if (nh->type == 3 && zapi_decode_u32(&d, &nh->ifindex) != 0)
                    return -8;
            }
            /* FRR emits a u32 weight immediately after the nexthop when
             * ZAPI_NEXTHOP_FLAG_WEIGHT (0x04) is set.  It is not part of
             * the gateway model, but must be consumed before the next
             * ECMP member is decoded. */
            if (nh->flags & 0x04) {
                uint64_t weight;
                if (zapi_decode_u64(&d, &weight) != 0) return -9;
                (void)weight;
            }
        }
    }
    return 0;
}

int zapi_serialize(const zapi_message_t *msg, uint8_t *buf, size_t buf_size)
{
    if (!msg || !buf) return -1;
    uint32_t total = ZAPI_HEADER_SIZE + (uint32_t)msg->payload_size;
    if (total > buf_size) return -2;

    /* Length */
    uint32_t length_n = htonl(total);
    memcpy(buf, &length_n, 4);

    /* Marker */
    uint16_t marker_n = htons(ZAPI_HEADER_MARKER);
    memcpy(buf + 4, &marker_n, 2);

    /* Version */
    buf[6] = ZAPI_VERSION;

    /* Command */
    uint16_t cmd_n = htons(msg->header.command);
    memcpy(buf + 7, &cmd_n, 2);

    /* Payload */
    if (msg->payload_size > 0 && msg->payload) {
        memcpy(buf + ZAPI_HEADER_SIZE, msg->payload, msg->payload_size);
    }
    return (int)total;
}

/* =========================================================================
 * Payload decoder
 * ========================================================================= */

void zapi_decoder_init(zapi_decoder_t *d, const uint8_t *data, size_t size)
{
    d->data = data;
    d->size = size;
    d->pos  = 0;
}

int zapi_decode_u8(zapi_decoder_t *d, uint8_t *out)
{
    if (d->pos + 1 > d->size) return -1;
    *out = d->data[d->pos];
    d->pos += 1;
    return 0;
}

int zapi_decode_u16(zapi_decoder_t *d, uint16_t *out)
{
    if (d->pos + 2 > d->size) return -1;
    memcpy(out, d->data + d->pos, 2);
    *out = ntohs(*out);
    d->pos += 2;
    return 0;
}

int zapi_decode_u32(zapi_decoder_t *d, uint32_t *out)
{
    if (d->pos + 4 > d->size) return -1;
    memcpy(out, d->data + d->pos, 4);
    *out = ntohl(*out);
    d->pos += 4;
    return 0;
}

int zapi_decode_u64(zapi_decoder_t *d, uint64_t *out)
{
    if (d->pos + 8 > d->size) return -1;
    uint32_t hi, lo;
    memcpy(&hi, d->data + d->pos, 4);
    memcpy(&lo, d->data + d->pos + 4, 4);
    *out = ((uint64_t)ntohl(hi) << 32) | ntohl(lo);
    d->pos += 8;
    return 0;
}

int zapi_decode_bytes(zapi_decoder_t *d, uint8_t *out, size_t n)
{
    if (d->pos + n > d->size) return -1;
    memcpy(out, d->data + d->pos, n);
    d->pos += n;
    return 0;
}

int zapi_decode_in_addr(zapi_decoder_t *d, uint8_t *out4)
{
    return zapi_decode_bytes(d, out4, 4);
}

int zapi_decode_in6_addr(zapi_decoder_t *d, uint8_t *out16)
{
    return zapi_decode_bytes(d, out16, 16);
}

/* =========================================================================
 * Payload encoder
 * ========================================================================= */

void zapi_encoder_init(zapi_encoder_t *e, uint8_t *data, size_t size)
{
    e->data = data;
    e->size = size;
    e->pos  = 0;
}

int zapi_encode_u8(zapi_encoder_t *e, uint8_t v)
{
    if (e->pos + 1 > e->size) return -1;
    e->data[e->pos] = v;
    e->pos += 1;
    return 0;
}

int zapi_encode_u16(zapi_encoder_t *e, uint16_t v)
{
    if (e->pos + 2 > e->size) return -1;
    uint16_t n = htons(v);
    memcpy(e->data + e->pos, &n, 2);
    e->pos += 2;
    return 0;
}

int zapi_encode_u32(zapi_encoder_t *e, uint32_t v)
{
    if (e->pos + 4 > e->size) return -1;
    uint32_t n = htonl(v);
    memcpy(e->data + e->pos, &n, 4);
    e->pos += 4;
    return 0;
}

int zapi_encode_u64(zapi_encoder_t *e, uint64_t v)
{
    if (e->pos + 8 > e->size) return -1;
    uint32_t hi = htonl((uint32_t)(v >> 32));
    uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFF));
    memcpy(e->data + e->pos, &hi, 4);
    memcpy(e->data + e->pos + 4, &lo, 4);
    e->pos += 8;
    return 0;
}

int zapi_encode_bytes(zapi_encoder_t *e, const uint8_t *src, size_t n)
{
    if (e->pos + n > e->size) return -1;
    memcpy(e->data + e->pos, src, n);
    e->pos += n;
    return 0;
}
