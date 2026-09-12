/*
 * DANOS-Open VPP Backend: Binary API Wire Codec (v0.3)
 *
 * Implements the VPP binary API wire format as specified by
 * FDio VPP src/vlibmemory/socket.c and the .api message definitions:
 *
 *   - Transport: AF_UNIX SOCK_STREAM (default /run/vpp/api.sock).
 *   - Framing: every message is prefixed with a 2-byte big-endian
 *     length field. The length counts the bytes that follow it.
 *   - Body layout: [u16 msg_id (big-endian)][message struct (packed,
 *     big-endian fields)].
 *   - Strings ("string name[N]" in .api files) are encoded as
 *     [u8 length][bytes] (length does not include the length byte).
 *
 * All multi-byte fields are big-endian ("network format") per the
 * VPP API Language specification.
 */

#ifndef DANOS_VPP_WIRE_H__
#define DANOS_VPP_WIRE_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed msg_id for the socket client handshake message. Legacy
 * socket transport control messages live below the dynamically
 * assigned range; sockclnt_create is 15 (0xF). */
#define VPP_MSG_ID_SOCKCLNT_CREATE 0x000F

/* Growable message buffer */
typedef struct {
    uint8_t *data;
    uint32_t len;
    uint32_t cap;
} vpp_buf_t;

void  vpp_buf_init(vpp_buf_t *b, uint32_t cap);
void  vpp_buf_free(vpp_buf_t *b);
void  vpp_buf_reset(vpp_buf_t *b);

/* Append fields (big-endian, packed) */
void vpp_buf_put_u8(vpp_buf_t *b, uint8_t v);
void vpp_buf_put_u16(vpp_buf_t *b, uint16_t v);
void vpp_buf_put_u32(vpp_buf_t *b, uint32_t v);
void vpp_buf_put_u64(vpp_buf_t *b, uint64_t v);
void vpp_buf_put_bytes(vpp_buf_t *b, const void *p, uint32_t n);
/* Length-prefixed string: [u8 len][bytes] */
void vpp_buf_put_string(vpp_buf_t *b, const char *s);

/* Sequential reader over a received body (after msg_id) */
typedef struct {
    const uint8_t *p;
    uint32_t len;
    uint32_t pos;
} vpp_reader_t;

void     vpp_reader_init(vpp_reader_t *r, const void *buf, uint32_t len);
bool     vpp_reader_ok(const vpp_reader_t *r);
uint8_t  vpp_rd_u8(vpp_reader_t *r);
uint16_t vpp_rd_u16(vpp_reader_t *r);
uint32_t vpp_rd_u32(vpp_reader_t *r);
uint64_t vpp_rd_u64(vpp_reader_t *r);
bool     vpp_rd_bytes(vpp_reader_t *r, void *out, uint32_t n);
/* Length-prefixed string; returns NULL-terminated copy (caller frees),
 * NULL on error */
char    *vpp_rd_string(vpp_reader_t *r, uint32_t max_len);

/* Socket framing: send body (msg_id + payload) with 2-byte BE length
 * prefix. Returns 0 on success. */
int vpp_wire_send_fd(int fd, uint16_t msg_id, const uint8_t *body, uint32_t body_len);

/* Receive one framed message. buf must be large enough for the frame.
 * Returns frame length (including 2-byte prefix), or -1 on error. */
int vpp_wire_recv_fd(int fd, uint8_t *buf, uint32_t buf_size);

/* name -> msg_id table (built from sockclnt_create_reply) */
#define VPP_MSG_TABLE_SIZE 4096  /* power of two */

typedef struct {
    char     name[72];
    uint16_t msg_id;
    bool     used;
} vpp_msg_table_entry_t;

typedef struct {
    vpp_msg_table_entry_t entries[VPP_MSG_TABLE_SIZE];
    uint32_t count;
} vpp_msg_table_t;

void vpp_msg_table_init(vpp_msg_table_t *t);
bool vpp_msg_table_add(vpp_msg_table_t *t, const char *name, uint16_t msg_id);
bool vpp_msg_table_lookup(const vpp_msg_table_t *t, const char *name, uint16_t *msg_id);
uint32_t vpp_msg_table_count(const vpp_msg_table_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_VPP_WIRE_H__ */
