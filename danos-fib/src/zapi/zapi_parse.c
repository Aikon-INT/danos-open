/*
 * DANOS-Open FIB: ZAPI Parser Implementation (C1)
 *
 * Clean-room implementation of FRR Zebra ZAPI wire protocol parsing.
 * Based on public ZAPI protocol documentation.
 */

#include "zapi.h"
#include <string.h>
#include <arpa/inet.h>

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
    out->payload = buf + FRR_ZAPI_HEADER_SIZE;
    out->payload_size = length - FRR_ZAPI_HEADER_SIZE;
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
