/*
 * DANOS-Open Management: HPACK (RFC 7541) codec for the gNMI gRPC server.
 *
 * Decoder supports: indexed fields, literal with/without incremental
 * indexing, never-indexed literals, dynamic table size updates, and the
 * dynamic table itself. Huffman-coded strings are rejected (our encoder
 * never emits them; clients that Huffman-encode require table support).
 *
 * Encoder emits literal-without-indexing with literal names — always
 * correct, table-free.
 */

#ifndef DANOS_HPACK_H__
#define DANOS_HPACK_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HPACK_MAX_ENTRIES 128
#define HPACK_DEFAULT_TABLE_SIZE 4096

typedef struct {
    char *name;    /* malloc'd */
    char *value;   /* malloc'd */
} hpack_entry_t;

typedef struct {
    hpack_entry_t entries[HPACK_MAX_ENTRIES];  /* newest first */
    size_t count;
    size_t size;                /* octets, per RFC 7541 4.1 */
    size_t max_size;
} hpack_dyn_table_t;

void hpack_dyn_init(hpack_dyn_table_t *t);
void hpack_dyn_free(hpack_dyn_table_t *t);
void hpack_dyn_set_max_size(hpack_dyn_table_t *t, size_t max);
/* Look up by HPACK index (1..61 static, >61 dynamic). Returns false if
 * out of range; fills name/value pointers (borrowed). */
bool hpack_lookup_index(hpack_dyn_table_t *t, uint64_t index,
                        const char **name, const char **value);
/* Insert (copies). Evicts as needed. */
bool hpack_dyn_insert(hpack_dyn_table_t *t, const char *name, const char *value);

/* ---- decoder -----------------------------------------------------------
 * Decodes a header block. For each header, calls cb(name, value, user).
 * Returns false on malformed input.
 */
typedef bool (*hpack_header_cb_t)(const char *name, const char *value, void *user);

bool hpack_decode(hpack_dyn_table_t *dyn,
                  const uint8_t *data, size_t len,
                  hpack_header_cb_t cb, void *user);

/* ---- encoder -----------------------------------------------------------
 * Appends a literal-without-indexing header to buf. Returns bytes used,
 * or -1 if the buffer is too small.
 */
int hpack_encode_literal(uint8_t *buf, size_t cap,
                         const char *name, const char *value);

/* Integer primitives (RFC 7541 5.1/5.2) — exported for tests */
int hpack_encode_int(uint8_t *buf, size_t cap, uint64_t v, uint8_t prefix_bits,
                     uint8_t prefix_pattern);
int hpack_encode_string(uint8_t *buf, size_t cap, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_HPACK_H__ */
