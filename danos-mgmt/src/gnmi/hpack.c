/*
 * DANOS-Open Management: HPACK (RFC 7541) codec.
 *
 * Static table per RFC 7541 Appendix B.
 */

#include "hpack.h"
#include "hpack_huffman.h"
#include <stdlib.h>
#include <string.h>

/* ---- static table (index 1..61) ---------------------------------------- */

static const struct { const char *name; const char *value; } k_static[] = {
    { ":authority", "" },
    { ":method", "GET" },
    { ":method", "POST" },
    { ":path", "/" },
    { ":path", "/index.html" },
    { ":scheme", "http" },
    { ":scheme", "https" },
    { ":status", "200" },
    { ":status", "204" },
    { ":status", "206" },
    { ":status", "304" },
    { ":status", "400" },
    { ":status", "404" },
    { ":status", "500" },
    { "accept-charset", "" },
    { "accept-encoding", "gzip, deflate" },
    { "accept-language", "" },
    { "accept-ranges", "" },
    { "accept", "" },
    { "access-control-allow-origin", "" },
    { "age", "" },
    { "allow", "" },
    { "authorization", "" },
    { "cache-control", "" },
    { "content-disposition", "" },
    { "content-encoding", "" },
    { "content-language", "" },
    { "content-length", "" },
    { "content-location", "" },
    { "content-range", "" },
    { "content-type", "" },
    { "cookie", "" },
    { "date", "" },
    { "etag", "" },
    { "expect", "" },
    { "expires", "" },
    { "from", "" },
    { "host", "" },
    { "if-match", "" },
    { "if-modified-since", "" },
    { "if-none-match", "" },
    { "if-range", "" },
    { "if-unmodified-since", "" },
    { "last-modified", "" },
    { "link", "" },
    { "location", "" },
    { "max-forwards", "" },
    { "proxy-authenticate", "" },
    { "proxy-authorization", "" },
    { "range", "" },
    { "referer", "" },
    { "refresh", "" },
    { "retry-after", "" },
    { "server", "" },
    { "set-cookie", "" },
    { "strict-transport-security", "" },
    { "transfer-encoding", "" },
    { "user-agent", "" },
    { "vary", "" },
    { "via", "" },
    { "www-authenticate", "" },
};
#define HPACK_STATIC_COUNT (sizeof(k_static) / sizeof(k_static[0]))

/* ---- dynamic table ------------------------------------------------------ */

void hpack_dyn_init(hpack_dyn_table_t *t)
{
    memset(t, 0, sizeof(*t));
    t->max_size = HPACK_DEFAULT_TABLE_SIZE;
}

void hpack_dyn_free(hpack_dyn_table_t *t)
{
    for (size_t i = 0; i < t->count; i++) {
        free(t->entries[i].name);
        free(t->entries[i].value);
    }
    memset(t, 0, sizeof(*t));
}

static size_t entry_size(const char *name, const char *value)
{
    return strlen(name) + strlen(value) + 32;
}

static void dyn_evict(hpack_dyn_table_t *t)
{
    while (t->size > t->max_size && t->count > 0) {
        /* entries[0] is newest; evict the oldest (last) */
        hpack_entry_t *e = &t->entries[t->count - 1];
        t->size -= entry_size(e->name, e->value);
        free(e->name);
        free(e->value);
        t->count--;
    }
}

void hpack_dyn_set_max_size(hpack_dyn_table_t *t, size_t max)
{
    t->max_size = max;
    dyn_evict(t);
}

bool hpack_dyn_insert(hpack_dyn_table_t *t, const char *name, const char *value)
{
    if (t->count >= HPACK_MAX_ENTRIES) return false;
    if (entry_size(name, value) > t->max_size) {
        /* entry too large: table becomes empty (RFC 7541 4.4) */
        dyn_evict(t);
        t->size = 0;
        /* fallthrough: drop everything, do not insert */
        for (size_t i = 0; i < t->count; i++) {
            free(t->entries[i].name);
            free(t->entries[i].value);
        }
        t->count = 0;
        return true;
    }
    /* shift to make room at front */
    memmove(&t->entries[1], &t->entries[0], t->count * sizeof(hpack_entry_t));
    t->entries[0].name = strdup(name);
    t->entries[0].value = strdup(value);
    if (!t->entries[0].name || !t->entries[0].value) {
        free(t->entries[0].name);
        free(t->entries[0].value);
        memmove(&t->entries[0], &t->entries[1], t->count * sizeof(hpack_entry_t));
        return false;
    }
    t->count++;
    t->size += entry_size(name, value);
    dyn_evict(t);
    return true;
}

bool hpack_lookup_index(hpack_dyn_table_t *t, uint64_t index,
                        const char **name, const char **value)
{
    if (index == 0) return false;
    if (index <= HPACK_STATIC_COUNT) {
        *name = k_static[index - 1].name;
        *value = k_static[index - 1].value;
        return true;
    }
    uint64_t d = index - HPACK_STATIC_COUNT - 1;  /* 0-based dynamic */
    if (d >= t->count) return false;
    *name = t->entries[d].name;
    *value = t->entries[d].value;
    return true;
}

/* ---- primitives --------------------------------------------------------- */

static int decode_int(const uint8_t *data, size_t len, size_t *pos,
                      uint8_t prefix_bits, uint64_t *out)
{
    if (*pos >= len) return -1;
    uint8_t mask = (uint8_t)((1u << prefix_bits) - 1);
    uint64_t v = data[*pos] & mask;
    (*pos)++;
    if (v < mask) {
        *out = v;
        return 0;
    }
    uint64_t m = 0;
    for (;;) {
        if (*pos >= len) return -1;
        uint8_t b = data[(*pos)++];
        v += (uint64_t)(b & 0x7F) << m;
        m += 7;
        if (!(b & 0x80)) break;
        if (m > 56) return -1;
    }
    *out = v;
    return 0;
}

int hpack_encode_int(uint8_t *buf, size_t cap, uint64_t v, uint8_t prefix_bits,
                     uint8_t prefix_pattern)
{
    uint8_t mask = (uint8_t)((1u << prefix_bits) - 1);
    size_t pos = 0;
    if (v < mask) {
        if (cap < 1) return -1;
        buf[pos++] = (uint8_t)(prefix_pattern | v);
    } else {
        if (cap < 2) return -1;
        buf[pos++] = (uint8_t)(prefix_pattern | mask);
        v -= mask;
        while (v >= 0x80) {
            if (pos + 2 > cap) return -1;
            buf[pos++] = (uint8_t)((v & 0x7F) | 0x80);
            v >>= 7;
        }
        buf[pos++] = (uint8_t)v;
    }
    return (int)pos;
}

int hpack_encode_string(uint8_t *buf, size_t cap, const char *s)
{
    size_t n = strlen(s);
    int k = hpack_encode_int(buf, cap, n, 7, 0x00);  /* no Huffman */
    if (k < 0) return -1;
    if (cap < (size_t)k + n) return -1;
    memcpy(buf + k, s, n);
    return k + (int)n;
}

int hpack_encode_literal(uint8_t *buf, size_t cap,
                         const char *name, const char *value)
{
    size_t pos = 0;
    /* literal without indexing, new name: 0x00 */
    if (cap < 1) return -1;
    buf[pos++] = 0x00;
    int k = hpack_encode_string(buf + pos, cap - pos, name);
    if (k < 0) return -1;
    pos += (size_t)k;
    k = hpack_encode_string(buf + pos, cap - pos, value);
    if (k < 0) return -1;
    pos += (size_t)k;
    return (int)pos;
}

/* ---- block decoder ------------------------------------------------------- */

static int decode_string(const uint8_t *data, size_t len, size_t *pos,
                         char **out)
{
    if (*pos >= len) return -1;
    bool huffman = (data[*pos] & 0x80) != 0;
    uint64_t n;
    if (decode_int(data, len, pos, 7, &n) < 0) return -1;
    if (*pos + n > len) return -1;

    if (huffman) {
        /* Huffman-coded: decoded length can exceed the encoded length
         * (up to 8/5 x, e.g. for random binary values), so allocate
         * AFTER decoding into a scratch buffer. */
        /* worst case: 8 bits per symbol expand to at most 8/5 x */
        size_t cap = (size_t)n * 8 / 5 + 8;
        uint8_t *tmp = malloc(cap);
        if (!tmp) return -1;
        size_t decoded = 0;
        bool ok = hpack_huffman_decode(data + *pos, (size_t)n,
                                       tmp, cap, &decoded);
        if (!ok) { free(tmp); return -1; }
        char *s = malloc(decoded + 1);
        if (!s) { free(tmp); return -1; }
        memcpy(s, tmp, decoded);
        s[decoded] = '\0';
        free(tmp);
        *pos += n;
        *out = s;
        return 0;
    }

    char *s = malloc((size_t)n + 1);
    if (!s) return -1;
    memcpy(s, data + *pos, n);
    s[n] = '\0';
    *pos += n;
    *out = s;
    return 0;
}

bool hpack_decode(hpack_dyn_table_t *dyn,
                  const uint8_t *data, size_t len,
                  hpack_header_cb_t cb, void *user)
{
    size_t pos = 0;
    while (pos < len) {
        uint8_t b = data[pos];
        if (b & 0x80) {
            /* indexed header field */
            uint64_t index;
            if (decode_int(data, len, &pos, 7, &index) < 0) return false;
            const char *name, *value;
            if (!hpack_lookup_index(dyn, index, &name, &value)) return false;
            if (!cb(name, value, user)) return false;
        } else if ((b & 0xC0) == 0x40) {
            /* literal with incremental indexing (6-bit prefix) */
            uint64_t index;
            if (decode_int(data, len, &pos, 6, &index) < 0) return false;
            char *name = NULL, *value = NULL;
            if (index == 0) {
                if (decode_string(data, len, &pos, &name) < 0) return false;
            } else {
                const char *n_, *v_;
                if (!hpack_lookup_index(dyn, index, &n_, &v_)) return false;
                name = strdup(n_);
                if (!name) return false;
            }
            if (decode_string(data, len, &pos, &value) < 0) {
                free(name);
                return false;
            }
            hpack_dyn_insert(dyn, name, value);
            bool ok = cb(name, value, user);
            free(name);
            free(value);
            if (!ok) return false;
        } else if ((b & 0xE0) == 0x20) {
            /* dynamic table size update (5-bit prefix) */
            uint64_t sz;
            if (decode_int(data, len, &pos, 5, &sz) < 0) return false;
            hpack_dyn_set_max_size(dyn, (size_t)sz);
        } else {
            /* literal without indexing / never indexed (4-bit prefix) */
            uint64_t index;
            if (decode_int(data, len, &pos, 4, &index) < 0) return false;
            char *name = NULL, *value = NULL;
            if (index == 0) {
                if (decode_string(data, len, &pos, &name) < 0) return false;
            } else {
                const char *n_, *v_;
                if (!hpack_lookup_index(dyn, index, &n_, &v_)) return false;
                name = strdup(n_);
                if (!name) return false;
            }
            if (decode_string(data, len, &pos, &value) < 0) {
                free(name);
                return false;
            }
            bool ok = cb(name, value, user);
            free(name);
            free(value);
            if (!ok) return false;
        }
    }
    return true;
}
