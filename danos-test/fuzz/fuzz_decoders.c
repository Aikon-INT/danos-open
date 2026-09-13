/*
 * v0.8 fuzz harness: mutational fuzzing of the hand-written binary
 * decoders (the project's most safety-critical code):
 *
 *   F1  HPACK header block decoder (static+dynamic table, Huffman)
 *   F2  gNMI protobuf: Get/Set/Subscribe request decoders
 *   F3  gNMI Path / TypedValue decoders
 *   F4  model path resolver against malformed paths
 *
 * Seeded with valid encodings then mutated (bit flips, truncation,
 * length-field corruption). Any crash under ASAN is a finding.
 * Deterministic PRNG so CI failures are reproducible.
 */

#include "../src/gnmi/hpack.h"
#include "../src/gnmi/hpack_huffman.h"
#include "../src/gnmi/gnmi_proto.h"
#include "../src/gnmi/model_paths.h"
#include <danos/core/object_registry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITERS 20000

static uint64_t rng_state = 0x12345678;
static uint32_t rnd(void)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(rng_state >> 33);
}

static uint8_t mut_buf[4096];

/* Copy src and apply 1-4 random mutations */
static size_t mutate(const uint8_t *src, size_t n)
{
    size_t len = n;
    if (len > sizeof(mut_buf)) len = sizeof(mut_buf);
    memcpy(mut_buf, src, len);
    int rounds = 1 + rnd() % 4;
    for (int i = 0; i < rounds && len > 0; i++) {
        switch (rnd() % 4) {
        case 0:  /* bit flip */
            mut_buf[rnd() % len] ^= 1u << (rnd() % 8);
            break;
        case 1:  /* random byte */
            mut_buf[rnd() % len] = (uint8_t)rnd();
            break;
        case 2:  /* truncate */
            if (len > 1) len = 1 + rnd() % (len - 1);
            break;
        case 3:  /* length-field corruption: bump a length-ish byte */
            mut_buf[rnd() % len] = (uint8_t)(0x80 | (rnd() % 64));
            break;
        }
    }
    return len;
}

static hpack_dyn_table_t dyn;

/* hpack_decode callback that accepts everything */
static bool sink_cb(const char *name, const char *value, void *user)
{
    (void)name; (void)value; (void)user;
    return true;
}

static void fuzz_hpack_cb(const uint8_t *seed, size_t n)
{
    for (int i = 0; i < ITERS / 4; i++) {
        size_t m = mutate(seed, n);
        hpack_dyn_init(&dyn);
        hpack_decode(&dyn, mut_buf, m, sink_cb, NULL);
        hpack_dyn_free(&dyn);
    }
}

static void fuzz_pb_requests(const uint8_t *seed, size_t n)
{
    gnmi_get_request_t gr;
    gnmi_set_request_t sr;
    gnmi_subscribe_request_t sub;
    for (int i = 0; i < ITERS / 4; i++) {
        size_t m = mutate(seed, n);
        memset(&gr, 0, sizeof(gr));
        memset(&sr, 0, sizeof(sr));
        memset(&sub, 0, sizeof(sub));
        gnmi_decode_get_request(mut_buf, m, &gr);
        gnmi_decode_set_request(mut_buf, m, &sr);
        gnmi_decode_subscribe_request(mut_buf, m, &sub);
    }
}

static void fuzz_pb_paths(const uint8_t *seed, size_t n)
{
    for (int i = 0; i < ITERS / 4; i++) {
        size_t m = mutate(seed, n);
        gnmi_path_t p;
        gnmi_typed_value_t v;
        gnmi_decode_path(mut_buf, m, &p);
        gnmi_decode_typed_value(mut_buf, m, &v);
        gnmi_path_from_str(&p, (const char *)mut_buf);
    }
}

static void fuzz_model_resolve(void)
{
    gnmi_path_t p;
    gnmi_model_binding_t b;
    char dotted[128];
    for (int i = 0; i < ITERS / 8; i++) {
        /* random elem soup */
        uint32_t n = rnd() % 6;
        memset(&p, 0, sizeof(p));
        for (uint32_t j = 0; j < n; j++) {
            snprintf(p.elems[j].name, GNMI_MAX_NAME, "e%u", rnd() % 5);
            if (rnd() % 3 == 0) {
                p.elems[j].has_key = true;
                snprintf(p.elems[j].key_name, GNMI_MAX_NAME, "k");
                snprintf(p.elems[j].key_value, GNMI_MAX_NAME, "v%u", rnd() % 4);
            }
        }
        p.elem_count = n;
        gnmi_model_resolve(&p, &b);
        /* also via dotted string with random bytes */
        for (size_t k = 0; k < sizeof(dotted) - 1; k++)
            dotted[k] = "abc/[]=" [rnd() % 7];
        dotted[sizeof(dotted) - 1] = '\0';
        gnmi_path_from_str(&p, dotted);
        gnmi_model_resolve(&p, &b);
    }
}

int main(void)
{
    /* seeds: known-good encodings from the conformance tests */
    static const uint8_t hpack_seed[] = {
        0x82, 0x86, 0x84, 0x41, 0x0f,
        'w','w','w','.','e','x','a','m','p','l','e','.','c','o','m',
    };
    static const uint8_t huff_seed[] = {
        0x00, 0x01, 'x', 0x82, 0x10, 0x01,   /* literal + huffman value */
        0x00, 0x03, 'a','b','c', 0x8c,       /* huffman, odd length */
    };
    static const uint8_t getreq_seed[] = {
        0x12, 0x0e, 0x1a, 0x0c, 0x0a, 0x0a,
        'i','n','t','e','r','f','a','c','e','s', 0x28, 0x04,
    };

    if (!g_default_store) g_default_store = danos_object_store_create(64);

    fuzz_hpack_cb(hpack_seed, sizeof(hpack_seed));
    fuzz_hpack_cb(huff_seed, sizeof(huff_seed));
    fuzz_pb_requests(getreq_seed, sizeof(getreq_seed));
    fuzz_pb_paths(getreq_seed, sizeof(getreq_seed));
    fuzz_model_resolve();

    printf("=== fuzz_decoders: %d iterations, no crashes ===\n", ITERS);
    return 0;
}
