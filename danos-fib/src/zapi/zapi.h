/*
 * DANOS-Open FIB: FRR ZAPI Protocol Definitions (C1)
 *
 * Defines FRR Zebra ZAPI message types and wire format.
 * This is a clean-room implementation based on the public FRR ZAPI
 * protocol documentation, not linked to FRR libraries.
 *
 * ZAPI wire format:
 *   [length:4][marker:2][version:1][command:2][payload:variable]
 *
 * We support ZAPI version 6 (FRR 10.x).
 */

#ifndef DANOS_FIB_ZAPI_H__
#define DANOS_FIB_ZAPI_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <danos/dpa.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ZAPI header marker */
#define ZAPI_HEADER_MARKER  0xFFFF
#define ZAPI_VERSION         6

/* ZAPI commands (subset relevant to DPA) */
typedef enum {
    ZEBRA_ROUTE_ADD        = 0,
    ZEBRA_ROUTE_DELETE     = 1,
    ZEBRA_REDISTRIBUTE_ADD = 8,
    ZEBRA_INTERFACE_ADD    = 20,
    ZEBRA_INTERFACE_DELETE = 21,
    ZEBRA_INTERFACE_SET_MTU = 24,
    ZEBRA_INTERFACE_UP     = 25,
    ZEBRA_INTERFACE_DOWN   = 26,
    ZEBRA_NEXTHOP_LOOKUP   = 40,
    ZEBRA_LABELS_ADD       = 60,
    ZEBRA_LABELS_DELETE    = 61,
    ZEBRA_BFD_DEST_REGISTER   = 80,
    ZEBRA_BFD_DEST_DEREGISTER = 81,
} zapi_command_t;

/* ZAPI message header */
typedef struct {
    uint32_t length;      /* total message length including header */
    uint16_t marker;      /* ZAPI_HEADER_MARKER */
    uint8_t  version;     /* ZAPI_VERSION */
    uint16_t command;     /* zapi_command_t */
} zapi_header_t;

#define ZAPI_HEADER_SIZE  9   /* 4+2+1+2 */

/* ZAPI message (header + payload) */
typedef struct {
    zapi_header_t header;
    const uint8_t *payload;
    size_t payload_size;
} zapi_message_t;

#define ZAPI_FRR_MESSAGE_NEXTHOP 0x01
#define ZAPI_FRR_MESSAGE_NHG     0x80
#define ZAPI_FRR_MAX_NEXTHOPS    64
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t family;
    uint8_t prefix_len;
    uint8_t prefix[16];
    uint32_t ifindex;
    uint8_t gateway[16];
    bool has_gateway;
} zapi_frr_nexthop_t;

typedef struct {
    uint8_t type;
    uint16_t instance;
    uint32_t flags;
    uint32_t message;
    uint8_t safi;
    uint8_t family;
    uint8_t prefix_len;
    uint8_t prefix[16];
    uint32_t nhg_id;
    uint16_t nexthop_count;
    zapi_frr_nexthop_t nexthops[ZAPI_FRR_MAX_NEXTHOPS];
} zapi_frr_route_t;

/* Parser: parse raw bytes into a ZAPI message.
 * Returns 0 on success, negative on error.
 * Does not copy payload; caller must keep buffer alive. */
int zapi_parse(const uint8_t *buf, size_t buf_size, zapi_message_t *out);

/* Parse the real FRR v6 zserv header. Payload remains FRR-native; callers
 * must use the FRR route decoder rather than the legacy mock mapper. */
int zapi_parse_frr(const uint8_t *buf, size_t buf_size, zapi_message_t *out);
int zapi_decode_frr_route(const zapi_message_t *msg, zapi_frr_route_t *out);

/* Serializer: serialize a ZAPI message into a buffer.
 * Returns bytes written on success, negative on error. */
int zapi_serialize(const zapi_message_t *msg, uint8_t *buf, size_t buf_size);

/* Payload decoder helpers (read from payload buffer) */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} zapi_decoder_t;

void zapi_decoder_init(zapi_decoder_t *d, const uint8_t *data, size_t size);
int  zapi_decode_u8(zapi_decoder_t *d, uint8_t *out);
int  zapi_decode_u16(zapi_decoder_t *d, uint16_t *out);
int  zapi_decode_u32(zapi_decoder_t *d, uint32_t *out);
int  zapi_decode_u64(zapi_decoder_t *d, uint64_t *out);
int  zapi_decode_bytes(zapi_decoder_t *d, uint8_t *out, size_t n);
int  zapi_decode_in_addr(zapi_decoder_t *d, uint8_t *out4);      /* 4 bytes */
int  zapi_decode_in6_addr(zapi_decoder_t *d, uint8_t *out16);    /* 16 bytes */

/* Payload encoder helpers (write into payload buffer) */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t pos;
} zapi_encoder_t;

void zapi_encoder_init(zapi_encoder_t *e, uint8_t *data, size_t size);
int  zapi_encode_u8(zapi_encoder_t *e, uint8_t v);
int  zapi_encode_u16(zapi_encoder_t *e, uint16_t v);
int  zapi_encode_u32(zapi_encoder_t *e, uint32_t v);
int  zapi_encode_u64(zapi_encoder_t *e, uint64_t v);
int  zapi_encode_bytes(zapi_encoder_t *e, const uint8_t *src, size_t n);

/* =========================================================================
 * ZAPI → DPA dispatch (defined in zapi_mapper.c)
 * ========================================================================= */

/* Dispatch a parsed ZAPI message to the appropriate DPA operation. */
danos_status_t zapi_dispatch(const zapi_message_t *msg, danos_tx_t *tx);

/* Live zebra session API (FRR integration entry point). */
int danos_zebra_session_init(void);
int danos_zebra_session_set_socket(const char *path);
int danos_zebra_session_connect(void);
int danos_zebra_session_register(uint8_t protocol, uint16_t instance);
int danos_zebra_session_reconnect(void);
void danos_zebra_session_disconnect(void);
int danos_zebra_session_get_state(void);
int danos_zebra_session_recv(uint8_t *buf, size_t buf_size, zapi_message_t *out);

/* Individual mappers (exposed for unit testing) */
danos_status_t zapi_map_interface_set_mtu(const zapi_message_t *msg,
                                          danos_tx_t *tx);
danos_status_t zapi_map_interface_set_admin(const zapi_message_t *msg,
                                            danos_tx_t *tx,
                                            bool admin_up);
danos_status_t zapi_map_nexthop_lookup(const zapi_message_t *msg,
                                       danos_tx_t *tx,
                                       danos_nexthop_t *out_nh);
danos_status_t zapi_map_redistribute_add(const zapi_message_t *msg,
                                         danos_tx_t *tx);
danos_status_t zapi_map_labels(const zapi_message_t *msg, danos_tx_t *tx,
                               bool is_add);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_FIB_ZAPI_H__ */
