/*
 * HPACK Huffman decode (RFC 7541 Appendix B) — interface.
 * Table data lives in hpack_huffman.c (generated from the Go standard
 * library's vendored golang.org/x/net/http2/hpack tables).
 */

#ifndef DANOS_HPACK_HUFFMAN_H__
#define DANOS_HPACK_HUFFMAN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decode a Huffman-encoded HPACK string. Output is written to out
 * (at most out_cap bytes). Returns true on success. Errors on:
 * EOS appearing mid-stream, overlong codes, or padding that is not
 * the all-ones prefix of EOS shorter than 8 bits (RFC 7541 5.2). */
bool hpack_huffman_decode(const uint8_t *data, size_t len,
                          uint8_t *out, size_t out_cap, size_t *out_len);

/* Encode (needed for tests/roundtrip): appends Huffman-coded bits of
 * `s` (with EOS padding). Returns bytes written or -1. */
int hpack_huffman_encode(uint8_t *buf, size_t cap, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_HPACK_HUFFMAN_H__ */
