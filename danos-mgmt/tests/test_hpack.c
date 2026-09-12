/*
 * Test: HPACK codec (v0.3)
 *
 * Includes the RFC 7541 Appendix C.6.1 known-answer vector for the
 * decoder (indexed fields + literal with incremental indexing), static
 * table lookups, dynamic table eviction, and encoder roundtrips.
 */

#include "../src/gnmi/hpack.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct {
    char pairs[16][2][128];
    int count;
} hdr_list_t;

/* v0.4 Huffman tests (defined after main) */
static int test_huffman_rfc_vectors(void);
static int test_huffman_roundtrip(void);
static int test_huffman_in_header_block(void);

static bool collect_cb(const char *name, const char *value, void *user)
{
    hdr_list_t *l = user;
    if (l->count >= 16) return false;
    snprintf(l->pairs[l->count][0], 128, "%s", name);
    snprintf(l->pairs[l->count][1], 128, "%s", value);
    l->count++;
    return true;
}

static int test_rfc7541_c61(void)
{
    /* C.6.1: :method GET (0x82), :scheme http (0x86), :path / (0x84),
     * :authority www.example.com (0x41 + literal, incremental indexing) */
    static const uint8_t block[] = {
        0x82, 0x86, 0x84, 0x41,
        0x0f, 'w','w','w','.','e','x','a','m','p','l','e','.','c','o','m',
    };
    hpack_dyn_table_t dyn;
    hpack_dyn_init(&dyn);

    hdr_list_t list;
    memset(&list, 0, sizeof(list));
    assert(hpack_decode(&dyn, block, sizeof(block), collect_cb, &list));
    assert(list.count == 4);
    assert(strcmp(list.pairs[0][0], ":method") == 0);
    assert(strcmp(list.pairs[0][1], "GET") == 0);
    assert(strcmp(list.pairs[1][0], ":scheme") == 0);
    assert(strcmp(list.pairs[2][1], "/") == 0);
    assert(strcmp(list.pairs[3][0], ":authority") == 0);
    assert(strcmp(list.pairs[3][1], "www.example.com") == 0);

    /* C.6.1 asserts the dynamic table now holds :authority */
    const char *n, *v;
    assert(hpack_lookup_index(&dyn, 62, &n, &v));
    assert(strcmp(n, ":authority") == 0);
    hpack_dyn_free(&dyn);

    printf("[PASS] test_rfc7541_c61 (known-answer vector)\n");
    return 0;
}

static int test_static_table(void)
{
    hpack_dyn_table_t dyn;
    hpack_dyn_init(&dyn);
    const char *n, *v;
    assert(hpack_lookup_index(&dyn, 1, &n, &v));
    assert(strcmp(n, ":authority") == 0);
    assert(hpack_lookup_index(&dyn, 2, &n, &v));
    assert(strcmp(n, ":method") == 0 && strcmp(v, "GET") == 0);
    assert(hpack_lookup_index(&dyn, 8, &n, &v));
    assert(strcmp(v, "200") == 0);
    assert(hpack_lookup_index(&dyn, 61, &n, &v));
    assert(strcmp(n, "www-authenticate") == 0);
    assert(!hpack_lookup_index(&dyn, 62, &n, &v));  /* empty dynamic */
    assert(!hpack_lookup_index(&dyn, 0, &n, &v));
    hpack_dyn_free(&dyn);
    printf("[PASS] test_static_table\n");
    return 0;
}

static int test_dynamic_table_eviction(void)
{
    hpack_dyn_table_t dyn;
    hpack_dyn_init(&dyn);
    hpack_dyn_set_max_size(&dyn, 128);  /* ~4 small entries */

    char name[32], value[32];
    for (int i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "h%d", i);
        snprintf(value, sizeof(value), "v%d", i);
        assert(hpack_dyn_insert(&dyn, name, value));
    }
    /* newest first */
    const char *n, *v;
    assert(hpack_lookup_index(&dyn, 62, &n, &v));
    assert(strcmp(v, "v9") == 0);
    /* count limited by size */
    assert(dyn.count < 10);
    assert(dyn.size <= 128);
    hpack_dyn_free(&dyn);
    printf("[PASS] test_dynamic_table_eviction\n");
    return 0;
}

static int test_encoder_roundtrip(void)
{
    uint8_t buf[256];
    int n = hpack_encode_literal(buf, sizeof(buf), "content-type",
                                 "application/grpc");
    assert(n > 0);

    hpack_dyn_table_t dyn;
    hpack_dyn_init(&dyn);
    hdr_list_t list;
    memset(&list, 0, sizeof(list));
    assert(hpack_decode(&dyn, buf, (size_t)n, collect_cb, &list));
    assert(list.count == 1);
    assert(strcmp(list.pairs[0][0], "content-type") == 0);
    assert(strcmp(list.pairs[0][1], "application/grpc") == 0);
    /* literal without indexing must not grow the table */
    assert(dyn.count == 0);
    hpack_dyn_free(&dyn);

    printf("[PASS] test_encoder_roundtrip\n");
    return 0;
}

static int test_integer_codec(void)
{
    uint8_t buf[16];
    /* 7-bit prefix, value 126 -> single byte 0x7E */
    assert(hpack_encode_int(buf, sizeof(buf), 126, 7, 0x00) == 1);
    assert(buf[0] == 0x7E);
    /* value == mask (127) must use the multi-byte form per RFC 7541 5.1 */
    assert(hpack_encode_int(buf, sizeof(buf), 127, 7, 0x00) == 2);
    assert(buf[0] == 0x7F && buf[1] == 0x00);
    /* 7-bit prefix, value 128 -> 0x7F 0x01 */
    assert(hpack_encode_int(buf, sizeof(buf), 128, 7, 0x00) == 2);
    assert(buf[0] == 0x7F && buf[1] == 0x01);
    /* 4-bit prefix pattern 0xF0? not valid; use 0x00 prefix pattern */
    assert(hpack_encode_int(buf, sizeof(buf), 14, 4, 0x00) == 1);
    assert(buf[0] == 0x0E);
    assert(hpack_encode_int(buf, sizeof(buf), 16, 4, 0x00) == 2);
    assert(buf[0] == 0x0F && buf[1] == 0x01);

    printf("[PASS] test_integer_codec\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_rfc7541_c61() != 0) failed++;
    if (test_static_table() != 0) failed++;
    if (test_dynamic_table_eviction() != 0) failed++;
    if (test_encoder_roundtrip() != 0) failed++;
    if (test_integer_codec() != 0) failed++;
    if (test_huffman_rfc_vectors() != 0) failed++;
    if (test_huffman_roundtrip() != 0) failed++;
    if (test_huffman_in_header_block() != 0) failed++;
    printf("=== hpack_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}

/* ---- Huffman (v0.4) ------------------------------------------------------ */
#include "../src/gnmi/hpack_huffman.h"

static int test_huffman_rfc_vectors(void)
{
    struct { const char *hex; const char *plain; } cases[] = {
        /* vectors generated with golang.org/x/net/http2/hpack
         * AppendHuffmanString (reference implementation) */
        { "f1e3c2e5f23a6ba0ab90f4ff", "www.example.com" },
        { "1001", "200" },
        { "1d75d0620d263d4c4d6564", "application/grpc" },
        { "25a849e95a728e42d8b771d169560899", "custom-header-value-123" },
        { "1f", "a" },
        { "626aa932f369d1918bc3ac719a8324c547", "/gnmi.gNMI/Capabilities" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t hexlen = strlen(cases[i].hex);
        size_t n = hexlen / 2;
        uint8_t data[64];
        for (size_t j = 0; j < n; j++) {
            unsigned v;
            sscanf(cases[i].hex + 2 * j, "%2x", &v);
            data[j] = (uint8_t)v;
        }
        uint8_t out[128];
        size_t out_len = 0;
        assert(hpack_huffman_decode(data, n, out, sizeof(out), &out_len));
        assert(out_len == strlen(cases[i].plain));
        out[out_len] = '\0';
        assert(strcmp((char *)out, cases[i].plain) == 0);
    }
    printf("[PASS] test_huffman_rfc_vectors (grpc-go reference encodings)\n");
    return 0;
}

static int test_huffman_roundtrip(void)
{
    const char *strs[] = {
        "www.example.com", "200", "application/grpc",
        "/gnmi.gNMI/Capabilities", "x", "", "grpc-status",
    };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        uint8_t enc[256];
        int n = hpack_huffman_encode(enc, sizeof(enc), strs[i]);
        assert(n > 0 || (n == 0 && strs[i][0] == '\0'));
        uint8_t out[256];
        size_t out_len = 0;
        assert(hpack_huffman_decode(enc, (size_t)(n > 0 ? n : 0),
                                    out, sizeof(out), &out_len));
        out[out_len] = '\0';
        assert(strcmp((char *)out, strs[i]) == 0);
    }
    printf("[PASS] test_huffman_roundtrip\n");
    return 0;
}

static int test_huffman_in_header_block(void)
{
    /* literal header with Huffman-coded value:
     * 0x00 (new name, literal w/o indexing)
     * name: 0x01 'x' (no huffman)
     * value: 0x82 (huffman flag | len 2) + huffman("200") = 10 01 */
    static const uint8_t block[] = { 0x00, 0x01, 'x', 0x82, 0x10, 0x01 };
    hpack_dyn_table_t dyn;
    hpack_dyn_init(&dyn);
    hdr_list_t list;
    memset(&list, 0, sizeof(list));
    assert(hpack_decode(&dyn, block, sizeof(block), collect_cb, &list));
    assert(list.count == 1);
    assert(strcmp(list.pairs[0][0], "x") == 0);
    assert(strcmp(list.pairs[0][1], "200") == 0);
    hpack_dyn_free(&dyn);
    printf("[PASS] test_huffman_in_header_block\n");
    return 0;
}
