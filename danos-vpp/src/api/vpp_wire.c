/*
 * DANOS-Open VPP Backend: Binary API Wire Codec (v0.3)
 */

#include "vpp_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

/* =========================================================================
 * Growable buffer
 * ========================================================================= */

void vpp_buf_init(vpp_buf_t *b, uint32_t cap)
{
    if (cap == 0) cap = 128;
    b->data = malloc(cap);
    b->cap = b->data ? cap : 0;
    b->len = 0;
}

void vpp_buf_free(vpp_buf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void vpp_buf_reset(vpp_buf_t *b)
{
    b->len = 0;
}

static bool buf_reserve(vpp_buf_t *b, uint32_t extra)
{
    if (b->len + extra <= b->cap) return true;
    uint32_t ncap = b->cap ? b->cap : 128;
    while (ncap < b->len + extra) ncap *= 2;
    uint8_t *nd = realloc(b->data, ncap);
    if (!nd) return false;
    b->data = nd;
    b->cap = ncap;
    return true;
}

void vpp_buf_put_u8(vpp_buf_t *b, uint8_t v)
{
    if (buf_reserve(b, 1)) b->data[b->len++] = v;
}

void vpp_buf_put_u16(vpp_buf_t *b, uint16_t v)
{
    if (buf_reserve(b, 2)) {
        b->data[b->len++] = (uint8_t)(v >> 8);
        b->data[b->len++] = (uint8_t)v;
    }
}

void vpp_buf_put_u32(vpp_buf_t *b, uint32_t v)
{
    if (buf_reserve(b, 4)) {
        b->data[b->len++] = (uint8_t)(v >> 24);
        b->data[b->len++] = (uint8_t)(v >> 16);
        b->data[b->len++] = (uint8_t)(v >> 8);
        b->data[b->len++] = (uint8_t)v;
    }
}

void vpp_buf_put_u64(vpp_buf_t *b, uint64_t v)
{
    vpp_buf_put_u32(b, (uint32_t)(v >> 32));
    vpp_buf_put_u32(b, (uint32_t)v);
}

void vpp_buf_put_bytes(vpp_buf_t *b, const void *p, uint32_t n)
{
    if (!p || n == 0) return;
    if (buf_reserve(b, n)) {
        memcpy(b->data + b->len, p, n);
        b->len += n;
    }
}

void vpp_buf_put_string(vpp_buf_t *b, const char *s)
{
    uint32_t n = s ? (uint32_t)strlen(s) : 0;
    if (n > 255) n = 255;  /* u8 length prefix */
    vpp_buf_put_u8(b, (uint8_t)n);
    vpp_buf_put_bytes(b, s, n);
}

/* =========================================================================
 * Reader
 * ========================================================================= */

void vpp_reader_init(vpp_reader_t *r, const void *buf, uint32_t len)
{
    r->p = buf;
    r->len = len;
    r->pos = 0;
}

bool vpp_reader_ok(const vpp_reader_t *r)
{
    return r->p != NULL && r->pos <= r->len;
}

static uint32_t rd_take(vpp_reader_t *r, uint32_t n, uint8_t *out)
{
    if (!r->p || r->pos + n > r->len) {
        r->pos = r->len + 1;  /* poison: mark over-run */
        return 0;
    }
    if (out) memcpy(out, r->p + r->pos, n);
    r->pos += n;
    return n;
}

uint8_t vpp_rd_u8(vpp_reader_t *r)
{
    uint8_t v = 0;
    rd_take(r, 1, &v);
    return v;
}

uint16_t vpp_rd_u16(vpp_reader_t *r)
{
    uint8_t t[2] = {0, 0};
    rd_take(r, 2, t);
    return (uint16_t)((t[0] << 8) | t[1]);
}

uint32_t vpp_rd_u32(vpp_reader_t *r)
{
    uint8_t t[4] = {0, 0, 0, 0};
    rd_take(r, 4, t);
    return ((uint32_t)t[0] << 24) | ((uint32_t)t[1] << 16) |
           ((uint32_t)t[2] << 8) | (uint32_t)t[3];
}

uint64_t vpp_rd_u64(vpp_reader_t *r)
{
    return ((uint64_t)vpp_rd_u32(r) << 32) | vpp_rd_u32(r);
}

bool vpp_rd_bytes(vpp_reader_t *r, void *out, uint32_t n)
{
    return rd_take(r, n, out) == n;
}

char *vpp_rd_string(vpp_reader_t *r, uint32_t max_len)
{
    uint8_t n = vpp_rd_u8(r);
    if (!vpp_reader_ok(r)) return NULL;
    uint32_t copy = n;
    if (copy > max_len) copy = max_len;
    char *s = malloc(copy + 1);
    if (!s) return NULL;
    if (!vpp_rd_bytes(r, s, copy)) {
        free(s);
        return NULL;
    }
    s[copy] = '\0';
    /* consume any remainder */
    if (n > copy && !vpp_rd_bytes(r, NULL, n - copy)) {
        free(s);
        return NULL;
    }
    return s;
}

/* =========================================================================
 * Socket framing
 * ========================================================================= */

static bool send_all(int fd, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n > 0) {
        ssize_t k = send(fd, b, n, MSG_NOSIGNAL);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return false;
        }
        b += k;
        n -= (uint32_t)k;
    }
    return true;
}

static bool recv_all(int fd, void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n > 0) {
        ssize_t k = recv(fd, b, n, 0);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return false;
        }
        b += k;
        n -= (uint32_t)k;
    }
    return true;
}

int vpp_wire_send_fd(int fd, uint16_t msg_id, const uint8_t *body, uint32_t body_len)
{
    if (fd < 0) return -1;
    uint32_t data_len = 2 + body_len;  /* msg_id + struct */
    if (data_len > 0xFFFFFFFFU - 16U) return -1;

    /* VPP msgbuf_t on 64-bit platforms: q pointer, data_len, gc timestamp. */
    uint8_t hdr[16] = {0};
    hdr[8] = (uint8_t)(data_len >> 24);
    hdr[9] = (uint8_t)(data_len >> 16);
    hdr[10] = (uint8_t)(data_len >> 8);
    hdr[11] = (uint8_t)data_len;
    if (!send_all(fd, hdr, sizeof(hdr))) return -1;
    uint8_t id[2] = {(uint8_t)(msg_id >> 8), (uint8_t)msg_id};
    if (!send_all(fd, id, sizeof(id))) return -1;
    if (body_len > 0 && body && !send_all(fd, body, body_len)) return -1;
    return 0;
}

int vpp_wire_recv_fd(int fd, uint8_t *buf, uint32_t buf_size)
{
    if (fd < 0 || !buf || buf_size < 4) return -1;

    uint8_t hdr[16];
    if (!recv_all(fd, hdr, sizeof(hdr))) return -1;
    uint32_t data_len = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16) |
                        ((uint32_t)hdr[10] << 8) | hdr[11];
    if (data_len == 0 || data_len > buf_size) return -1;
    if (!recv_all(fd, buf, data_len)) return -1;
    return (int)data_len;
}

/* =========================================================================
 * name -> msg_id table
 * ========================================================================= */

static uint32_t name_hash(const char *s)
{
    uint64_t h = 1469598103934665603ULL;  /* FNV-1a */
    for (const char *p = s; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 1099511628211ULL;
    }
    return (uint32_t)(h ^ (h >> 32));
}

void vpp_msg_table_init(vpp_msg_table_t *t)
{
    memset(t, 0, sizeof(*t));
}

bool vpp_msg_table_add(vpp_msg_table_t *t, const char *name, uint16_t msg_id)
{
    if (!t || !name || !name[0]) return false;
    uint32_t mask = VPP_MSG_TABLE_SIZE - 1;
    uint32_t i = name_hash(name) & mask;
    for (uint32_t probe = 0; probe < VPP_MSG_TABLE_SIZE; probe++) {
        vpp_msg_table_entry_t *e = &t->entries[i];
        if (!e->used) {
            snprintf(e->name, sizeof(e->name), "%s", name);
            e->msg_id = msg_id;
            e->used = true;
            t->count++;
            return true;
        }
        if (strcmp(e->name, name) == 0) {
            e->msg_id = msg_id;  /* update */
            return true;
        }
        i = (i + 1) & mask;
    }
    return false;
}

bool vpp_msg_table_lookup(const vpp_msg_table_t *t, const char *name, uint16_t *msg_id)
{
    if (!t || !name) return false;
    uint32_t mask = VPP_MSG_TABLE_SIZE - 1;
    uint32_t i = name_hash(name) & mask;
    for (uint32_t probe = 0; probe < VPP_MSG_TABLE_SIZE; probe++) {
        const vpp_msg_table_entry_t *e = &t->entries[i];
        if (!e->used) return false;
        if (strcmp(e->name, name) == 0) {
            if (msg_id) *msg_id = e->msg_id;
            return true;
        }
        i = (i + 1) & mask;
    }
    return false;
}

uint32_t vpp_msg_table_count(const vpp_msg_table_t *t)
{
    return t ? t->count : 0;
}
