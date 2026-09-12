/*
 * Test: gNMI protobuf codec (v0.3)
 *
 * Verifies the proto3 wire format against hand-computed byte sequences
 * (per openconfig gnmi.proto field numbers) plus roundtrips.
 */

#include "../src/gnmi/gnmi_proto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static int test_pb_primitives(void)
{
    uint8_t buf[64];
    gnmi_pb_t w;
    gnmi_pb_init(&w, buf, sizeof(buf));

    /* varint 300 = 0xAC 0x02 */
    gnmi_pb_put_varint(&w, 300);
    assert(w.len == 2 && buf[0] == 0xAC && buf[1] == 0x02);

    /* string field 3 "ab" = 0x1A 0x02 0x61 0x62 */
    gnmi_pb_t w2;
    uint8_t b2[16];
    gnmi_pb_init(&w2, b2, sizeof(b2));
    gnmi_pb_put_string(&w2, 3, "ab");
    assert(w2.len == 4);
    assert(b2[0] == 0x1A && b2[1] == 0x02 && b2[2] == 'a' && b2[3] == 'b');

    printf("[PASS] test_pb_primitives\n");
    return 0;
}

static int test_path_encoding(void)
{
    /* Path{ origin omitted, elem=[{name:"interfaces"},{name:"interface",
     * key{name="eth0"}}] }
     * elem1: 0x0A 0x0A "interfaces"
     * elem2: name 0x0A 0x09 "interface"; key entry: 0x12 len(1+4+1+4=12?)
     *   key entry: field1 "name" (0x0A 0x04 6e616d65) + field2 "eth0"
     *   (0x12 0x04 65746830) = 14 bytes -> 0x12 0x0E ...
     * PathElem2 = 0x0A 0x09 + "interface"(9) + 0x12 0x0E + 14 = 2+9+2+14=27
     * Path = field3 tag (0x1A) + len... */
    gnmi_path_t p;
    assert(gnmi_path_from_str(&p, "/interfaces/interface[name=eth0]"));
    assert(p.elem_count == 2);
    assert(strcmp(p.elems[0].name, "interfaces") == 0);
    assert(p.elems[1].has_key);
    assert(strcmp(p.elems[1].key_name, "name") == 0);
    assert(strcmp(p.elems[1].key_value, "eth0") == 0);

    uint8_t buf[128];
    gnmi_pb_t w;
    gnmi_pb_init(&w, buf, sizeof(buf));
    gnmi_encode_path(&w, 1, &p);

    /* decode back */
    gnmi_path_t back;
    /* skip outer tag+len */
    const uint8_t *body = buf + 2;  /* field1 tag(0x0A) + 1-byte len */
    size_t body_len = buf[1];
    assert(buf[0] == 0x0A);
    assert(gnmi_decode_path(body, body_len, &back));
    assert(back.elem_count == 2);
    assert(strcmp(back.elems[1].key_value, "eth0") == 0);

    printf("[PASS] test_path_encoding\n");
    return 0;
}

static int test_get_request_decode(void)
{
    /* Hand-built GetRequest:
     * path (field 2) = Path{elem:[{name:"interfaces"}]}
     *   Path body: 0x1A 0x0C 0x0A 0x0A "interfaces"  (elem tag 3, len 12:
     *     PathElem = 0x0A 0x0A + 10 bytes)
     *   -> field2: 0x12 0x0E + 14 bytes
     * encoding (field 5) = JSON_IETF(4): 0x28 0x04
     */
    uint8_t req[] = {
        0x12, 0x0E,
            0x1A, 0x0C, 0x0A, 0x0A,
            'i','n','t','e','r','f','a','c','e','s',
        0x28, 0x04,
    };
    gnmi_get_request_t gr;
    assert(gnmi_decode_get_request(req, sizeof(req), &gr));
    assert(gr.path_count == 1);
    assert(strcmp(gr.paths[0].elems[0].name, "interfaces") == 0);
    assert(gr.encoding == GNMI_ENC_JSON_IETF);

    printf("[PASS] test_get_request_decode (exact wire bytes)\n");
    return 0;
}

static int test_get_response_roundtrip(void)
{
    gnmi_notification_t n;
    memset(&n, 0, sizeof(n));
    n.timestamp = 1234567;
    n.update_count = 1;
    assert(gnmi_path_from_str(&n.updates[0].path, "/interfaces"));
    n.updates[0].val.kind = GNMI_VAL_JSON_IETF;
    strcpy(n.updates[0].val.s, "{\"ifindex\":1}");

    uint8_t buf[256];
    gnmi_pb_t w;
    gnmi_pb_init(&w, buf, sizeof(buf));
    assert(gnmi_encode_get_response(&w, &n, 1));

    /* decode the notification back */
    assert(buf[0] == 0x0A);   /* GetResponse.notification = field 1, LEN */
    gnmi_notification_t back;
    memset(&back, 0, sizeof(back));
    /* manual decode: notification body = buf[2..] */
    gnmi_pb_reader_t r;
    gnmi_pbr_init(&r, buf + 2, buf[1]);
    uint32_t field, wire;
    bool got_ts = false, got_upd = false;
    while ((field = gnmi_pbr_tag(&r, &wire)) != 0) {
        if (field == 1) { assert(gnmi_pbr_varint(&r) == 1234567); got_ts = true; }
        else if (field == 4) {
            const uint8_t *d; size_t dn;
            assert(gnmi_pbr_bytes(&r, &d, &dn));
            /* Update: path=1, val=3 */
            gnmi_update_t u;
            memset(&u, 0, sizeof(u));
            gnmi_pb_reader_t ur;
            gnmi_pbr_init(&ur, d, dn);
            uint32_t uf, uw;
            while ((uf = gnmi_pbr_tag(&ur, &uw)) != 0) {
                const uint8_t *ud; size_t un;
                if (uf == 1) {
                    assert(gnmi_pbr_bytes(&ur, &ud, &un));
                    assert(gnmi_decode_path(ud, un, &u.path));
                } else if (uf == 3) {
                    assert(gnmi_pbr_bytes(&ur, &ud, &un));
                    assert(gnmi_decode_typed_value(ud, un, &u.val));
                } else gnmi_pbr_skip(&ur, uw);
            }
            assert(strcmp(u.path.elems[0].name, "interfaces") == 0);
            assert(u.val.kind == GNMI_VAL_JSON_IETF);
            assert(strcmp(u.val.s, "{\"ifindex\":1}") == 0);
            got_upd = true;
        } else gnmi_pbr_skip(&r, wire);
    }
    assert(got_ts && got_upd && !r.err);

    printf("[PASS] test_get_response_roundtrip\n");
    return 0;
}

static int test_set_request_decode(void)
{
    /* SetRequest{ update (field 4) = Update{ path=Path{elem: if/name=eth0},
     * val=TypedValue{ string_val=1 "up" } } } built via our own encoder
     * (roundtrip) */
    gnmi_set_request_t in;
    memset(&in, 0, sizeof(in));
    in.update_count = 1;
    assert(gnmi_path_from_str(&in.updates[0].path, "interfaces/interface[name=eth0]"));
    in.updates[0].val.kind = GNMI_VAL_STRING;
    strcpy(in.updates[0].val.s, "up");

    uint8_t buf[256];
    gnmi_pb_t w;
    gnmi_pb_init(&w, buf, sizeof(buf));
    /* emulate SetRequest encoding: field 4 repeated Update */
    extern void gnmi_encode_notification(gnmi_pb_t*, uint32_t, const gnmi_notification_t*);
    (void)gnmi_encode_notification;
    /* encode Update manually via encode helpers */
    size_t saved = gnmi_pb_begin_nested(&w, 4);
    gnmi_encode_path(&w, 1, &in.updates[0].path);
    gnmi_encode_typed_value(&w, 3, &in.updates[0].val);
    gnmi_pb_end_nested(&w, saved);

    gnmi_set_request_t out;
    assert(gnmi_decode_set_request(buf, w.len, &out));
    assert(out.update_count == 1);
    assert(strcmp(out.updates[0].path.elems[1].key_value, "eth0") == 0);
    assert(out.updates[0].val.kind == GNMI_VAL_STRING);
    assert(strcmp(out.updates[0].val.s, "up") == 0);

    printf("[PASS] test_set_request_decode\n");
    return 0;
}

static int test_capabilities_encoding(void)
{
    uint8_t buf[256];
    gnmi_pb_t w;
    gnmi_pb_init(&w, buf, sizeof(buf));
    gnmi_model_data_t m = { "openconfig-interfaces", "OpenConfig", "2.4.1" };
    gnmi_encoding_t e = GNMI_ENC_JSON_IETF;
    assert(gnmi_encode_capabilities_response(&w, &m, 1, &e, 1, "0.3.0"));

    /* verify: model (field1) then encoding enum (field2, varint 4) then
     * version string (field3) */
    gnmi_pb_reader_t r;
    gnmi_pbr_init(&r, buf, w.len);
    uint32_t field, wire;
    int seen_model = 0, seen_enc = 0, seen_ver = 0;
    while ((field = gnmi_pbr_tag(&r, &wire)) != 0) {
        if (field == 1 && wire == 2) {
            const uint8_t *d; size_t n;
            assert(gnmi_pbr_bytes(&r, &d, &n));
            /* ModelData name=1 */
            gnmi_pb_reader_t mr;
            gnmi_pbr_init(&mr, d, n);
            uint32_t mf, mw;
            while ((mf = gnmi_pbr_tag(&mr, &mw)) != 0) {
                const uint8_t *md; size_t mn;
                if (mf == 1 && gnmi_pbr_bytes(&mr, &md, &mn)) {
                    assert(mn == 21);
                    assert(memcmp(md, "openconfig-interfaces", 21) == 0);
                } else gnmi_pbr_skip(&mr, mw);
            }
            seen_model++;
        } else if (field == 2 && wire == 0) {
            assert(gnmi_pbr_varint(&r) == 4);
            seen_enc++;
        } else if (field == 3 && wire == 2) {
            const uint8_t *d; size_t n;
            assert(gnmi_pbr_bytes(&r, &d, &n));
            assert(n == 5 && memcmp(d, "0.3.0", 5) == 0);
            seen_ver++;
        } else gnmi_pbr_skip(&r, wire);
    }
    assert(seen_model == 1 && seen_enc == 1 && seen_ver == 1);

    printf("[PASS] test_capabilities_encoding\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_pb_primitives() != 0) failed++;
    if (test_path_encoding() != 0) failed++;
    if (test_get_request_decode() != 0) failed++;
    if (test_get_response_roundtrip() != 0) failed++;
    if (test_set_request_decode() != 0) failed++;
    if (test_capabilities_encoding() != 0) failed++;
    printf("=== gnmi_proto_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
