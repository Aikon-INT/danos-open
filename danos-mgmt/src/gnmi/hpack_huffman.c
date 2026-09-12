/*
 * RFC 7541 Appendix B Huffman code table.
 * Generated from golang.org/x/net/http2/hpack tables.go
 * (vendored in the Go standard library).
 * EOS (symbol 256) is the all-ones code of 30 bits: 0x3fffffff.
 */
#include "hpack_huffman.h"
#include <string.h>

const uint32_t hpack_huffman_codes[257] = {
    0x00001ff8, 0x007fffd8, 0x0fffffe2, 0x0fffffe3, 0x0fffffe4, 0x0fffffe5,
    0x0fffffe6, 0x0fffffe7, 0x0fffffe8, 0x00ffffea, 0x3ffffffc, 0x0fffffe9,
    0x0fffffea, 0x3ffffffd, 0x0fffffeb, 0x0fffffec, 0x0fffffed, 0x0fffffee,
    0x0fffffef, 0x0ffffff0, 0x0ffffff1, 0x0ffffff2, 0x3ffffffe, 0x0ffffff3,
    0x0ffffff4, 0x0ffffff5, 0x0ffffff6, 0x0ffffff7, 0x0ffffff8, 0x0ffffff9,
    0x0ffffffa, 0x0ffffffb, 0x00000014, 0x000003f8, 0x000003f9, 0x00000ffa,
    0x00001ff9, 0x00000015, 0x000000f8, 0x000007fa, 0x000003fa, 0x000003fb,
    0x000000f9, 0x000007fb, 0x000000fa, 0x00000016, 0x00000017, 0x00000018,
    0x00000000, 0x00000001, 0x00000002, 0x00000019, 0x0000001a, 0x0000001b,
    0x0000001c, 0x0000001d, 0x0000001e, 0x0000001f, 0x0000005c, 0x000000fb,
    0x00007ffc, 0x00000020, 0x00000ffb, 0x000003fc, 0x00001ffa, 0x00000021,
    0x0000005d, 0x0000005e, 0x0000005f, 0x00000060, 0x00000061, 0x00000062,
    0x00000063, 0x00000064, 0x00000065, 0x00000066, 0x00000067, 0x00000068,
    0x00000069, 0x0000006a, 0x0000006b, 0x0000006c, 0x0000006d, 0x0000006e,
    0x0000006f, 0x00000070, 0x00000071, 0x00000072, 0x000000fc, 0x00000073,
    0x000000fd, 0x00001ffb, 0x0007fff0, 0x00001ffc, 0x00003ffc, 0x00000022,
    0x00007ffd, 0x00000003, 0x00000023, 0x00000004, 0x00000024, 0x00000005,
    0x00000025, 0x00000026, 0x00000027, 0x00000006, 0x00000074, 0x00000075,
    0x00000028, 0x00000029, 0x0000002a, 0x00000007, 0x0000002b, 0x00000076,
    0x0000002c, 0x00000008, 0x00000009, 0x0000002d, 0x00000077, 0x00000078,
    0x00000079, 0x0000007a, 0x0000007b, 0x00007ffe, 0x000007fc, 0x00003ffd,
    0x00001ffd, 0x0ffffffc, 0x000fffe6, 0x003fffd2, 0x000fffe7, 0x000fffe8,
    0x003fffd3, 0x003fffd4, 0x003fffd5, 0x007fffd9, 0x003fffd6, 0x007fffda,
    0x007fffdb, 0x007fffdc, 0x007fffdd, 0x007fffde, 0x00ffffeb, 0x007fffdf,
    0x00ffffec, 0x00ffffed, 0x003fffd7, 0x007fffe0, 0x00ffffee, 0x007fffe1,
    0x007fffe2, 0x007fffe3, 0x007fffe4, 0x001fffdc, 0x003fffd8, 0x007fffe5,
    0x003fffd9, 0x007fffe6, 0x007fffe7, 0x00ffffef, 0x003fffda, 0x001fffdd,
    0x000fffe9, 0x003fffdb, 0x003fffdc, 0x007fffe8, 0x007fffe9, 0x001fffde,
    0x007fffea, 0x003fffdd, 0x003fffde, 0x00fffff0, 0x001fffdf, 0x003fffdf,
    0x007fffeb, 0x007fffec, 0x001fffe0, 0x001fffe1, 0x003fffe0, 0x001fffe2,
    0x007fffed, 0x003fffe1, 0x007fffee, 0x007fffef, 0x000fffea, 0x003fffe2,
    0x003fffe3, 0x003fffe4, 0x007ffff0, 0x003fffe5, 0x003fffe6, 0x007ffff1,
    0x03ffffe0, 0x03ffffe1, 0x000fffeb, 0x0007fff1, 0x003fffe7, 0x007ffff2,
    0x003fffe8, 0x01ffffec, 0x03ffffe2, 0x03ffffe3, 0x03ffffe4, 0x07ffffde,
    0x07ffffdf, 0x03ffffe5, 0x00fffff1, 0x01ffffed, 0x0007fff2, 0x001fffe3,
    0x03ffffe6, 0x07ffffe0, 0x07ffffe1, 0x03ffffe7, 0x07ffffe2, 0x00fffff2,
    0x001fffe4, 0x001fffe5, 0x03ffffe8, 0x03ffffe9, 0x0ffffffd, 0x07ffffe3,
    0x07ffffe4, 0x07ffffe5, 0x000fffec, 0x00fffff3, 0x000fffed, 0x001fffe6,
    0x003fffe9, 0x001fffe7, 0x001fffe8, 0x007ffff3, 0x003fffea, 0x003fffeb,
    0x01ffffee, 0x01ffffef, 0x00fffff4, 0x00fffff5, 0x03ffffea, 0x007ffff4,
    0x03ffffeb, 0x07ffffe6, 0x03ffffec, 0x03ffffed, 0x07ffffe7, 0x07ffffe8,
    0x07ffffe9, 0x07ffffea, 0x07ffffeb, 0x0ffffffe, 0x07ffffec, 0x07ffffed,
    0x07ffffee, 0x07ffffef, 0x07fffff0, 0x03ffffee,
    0x3fffffff, /* EOS (sym 256) */
};

const uint8_t hpack_huffman_lens[257] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
    28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
     6, 10, 10, 12, 13,  6,  8, 11, 10, 10,  8, 11,  8,  6,  6,  6,
     5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  8, 15,  6, 12, 10,
    13,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  8,  7,  8, 13, 19, 13, 14,  6,
    15,  5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,
     6,  7,  6,  5,  5,  6,  7,  7,  7,  7,  7, 15, 11, 14, 13, 28,
    20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
    24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
    22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
    21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
    26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25,
    19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27,
    20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
    26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
    30, /* EOS */
};

/* =========================================================================
 * Canonical decoder support
 *
 * The RFC table is canonical: codes of the same bit length are
 * consecutive. Build per-length [min_code, count, first_sym] ranges and
 * decode bit-by-bit.
 * ========================================================================= */

#define HUF_MAX_LEN 30

/* Symbols within a bit length are ordered by code but NOT contiguous
 * (e.g. 'X' is an 8-bit code, leaving a gap in the 7-bit symbol run),
 * so each length carries an explicit symbol table. */
#define HUF_MAX_SYMS 40

typedef struct {
    uint32_t min_code;   /* smallest code at this length */
    uint32_t count;      /* number of codes at this length */
    uint16_t syms[HUF_MAX_SYMS];  /* symbols ordered by code (256=EOS) */
} huf_range_t;

static huf_range_t g_ranges[HUF_MAX_LEN + 1];
static bool g_ranges_built = false;

/* The HPACK table is prefix-free and codes are contiguous within each
 * bit length, but it is NOT complete — lengths such as 9 and 16..18
 * have no codes — so min_code cannot be reconstructed from lengths
 * alone. Take it from the actual codes. */
static void huf_build_ranges(void)
{
    if (g_ranges_built) return;
    memset(g_ranges, 0, sizeof(g_ranges));

    for (int s = 0; s < 257; s++) {
        uint8_t l = hpack_huffman_lens[s];
        uint32_t c = hpack_huffman_codes[s];   /* right-aligned */
        huf_range_t *r = &g_ranges[l];
        if (r->count == 0 || c < r->min_code) {
            r->min_code = c;
        }
        if (r->count < HUF_MAX_SYMS) {
            r->syms[r->count] = (uint8_t)s;
        }
        r->count++;
    }
    g_ranges_built = true;
}

bool hpack_huffman_decode(const uint8_t *data, size_t len,
                          uint8_t *out, size_t out_cap, size_t *out_len)
{
    huf_build_ranges();

    uint32_t code = 0;
    uint32_t clen = 0;
    size_t o = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (int bit = 7; bit >= 0; bit--) {
            code = (code << 1) | ((b >> bit) & 1);
            clen++;

            if (clen > HUF_MAX_LEN) return false;

            const huf_range_t *r = &g_ranges[clen];
            if (r->count > 0 &&
                code >= r->min_code &&
                code < r->min_code + r->count) {
                uint16_t sym = r->syms[code - r->min_code];
                if (sym == 256) return false;  /* EOS mid-stream */
                if (o >= out_cap) return false;
                out[o++] = (uint8_t)sym;
                code = 0;
                clen = 0;
            }
        }
    }

    /* padding: leftover bits must be all ones and fewer than 8
     * (they are the start of the EOS all-ones code) */
    if (clen > 0) {
        if (clen >= 8) return false;
        /* code has clen bits; all must be 1 */
        uint32_t ones = (1u << clen) - 1;
        if (code != ones) return false;
    }

    *out_len = o;
    return true;
}

int hpack_huffman_encode(uint8_t *buf, size_t cap, const char *s)
{
    huf_build_ranges();

    uint64_t acc = 0;
    uint32_t nbits = 0;
    size_t o = 0;

    for (const char *p = s; *p; p++) {
        uint8_t sym = (uint8_t)*p;
        uint8_t l = hpack_huffman_lens[sym];
        uint32_t c = hpack_huffman_codes[sym];  /* right-aligned */
        acc = (acc << l) | c;
        nbits += l;
        while (nbits >= 8) {
            if (o >= cap) return -1;
            buf[o++] = (uint8_t)(acc >> (nbits - 8));
            nbits -= 8;
            acc &= (1u << nbits) - 1;
        }
    }
    if (nbits > 0) {
        /* pad with EOS (all ones) */
        acc = (acc << (8 - nbits)) | ((1u << (8 - nbits)) - 1);
        if (o >= cap) return -1;
        buf[o++] = (uint8_t)acc;
    }
    return (int)o;
}
