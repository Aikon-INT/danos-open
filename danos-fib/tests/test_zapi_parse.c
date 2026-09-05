/*
 * Test: ZAPI Parser (C1)
 * Verify ZAPI message parse/serialize round-trip.
 */
#include "../src/zapi/zapi.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int test_zapi_parse_serialize(void)
{
    /* Build a ZAPI message: ROUTE_ADD with small payload */
    uint8_t payload[] = {0x00, 0x00, 0x00, 0x64,  /* vrf_id=100 */
                         0x04,                      /* family=IPv4 */
                         0x18,                      /* prefix_len=24 */
                         0x0A, 0x00, 0x00, 0x00,   /* 10.0.0.0 */
                         0x02,                      /* proto=static */
                         0x01,                      /* admin_dist=1 */
                         0x00, 0x00, 0x00, 0x00,   /* metric=0 */
                         0x00};                     /* nh_count=0 */

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_ROUTE_ADD;
    msg.payload = payload;
    msg.payload_size = sizeof(payload);

    /* Serialize */
    uint8_t buf[256];
    int written = zapi_serialize(&msg, buf, sizeof(buf));
    assert(written > 0);
    assert(written == (int)(ZAPI_HEADER_SIZE + sizeof(payload)));

    /* Parse back */
    zapi_message_t parsed;
    int rc = zapi_parse(buf, (size_t)written, &parsed);
    assert(rc == 0);
    assert(parsed.header.length == (uint32_t)written);
    assert(parsed.header.marker == ZAPI_HEADER_MARKER);
    assert(parsed.header.version == ZAPI_VERSION);
    assert(parsed.header.command == ZEBRA_ROUTE_ADD);
    assert(parsed.payload_size == sizeof(payload));
    assert(memcmp(parsed.payload, payload, sizeof(payload)) == 0);

    printf("[PASS] test_zapi_parse_serialize: round-trip works\n");
    return 0;
}

int test_zapi_decoder(void)
{
    /* Test decoder helpers */
    uint8_t data[] = {0x01,                       /* u8 = 1 */
                      0x00, 0x64,                 /* u16 = 100 */
                      0x00, 0x00, 0x00, 0xC8,    /* u32 = 200 */
                      0x0A, 0x00, 0x00, 0x01};   /* IPv4 10.0.0.1 */

    zapi_decoder_t d;
    zapi_decoder_init(&d, data, sizeof(data));

    uint8_t u8;
    assert(zapi_decode_u8(&d, &u8) == 0);
    assert(u8 == 1);

    uint16_t u16;
    assert(zapi_decode_u16(&d, &u16) == 0);
    assert(u16 == 100);

    uint32_t u32;
    assert(zapi_decode_u32(&d, &u32) == 0);
    assert(u32 == 200);

    uint8_t ip[4];
    assert(zapi_decode_in_addr(&d, ip) == 0);
    assert(ip[0] == 10 && ip[1] == 0 && ip[2] == 0 && ip[3] == 1);

    /* Past end */
    uint8_t extra;
    assert(zapi_decode_u8(&d, &extra) != 0);

    printf("[PASS] test_zapi_decoder: decode primitives work\n");
    return 0;
}

int test_zapi_invalid(void)
{
    /* Too short */
    uint8_t short_buf[4] = {0};
    zapi_message_t msg;
    assert(zapi_parse(short_buf, sizeof(short_buf), &msg) != 0);

    /* Bad marker */
    uint8_t bad_marker[16] = {0};
    bad_marker[4] = 0x00; bad_marker[5] = 0x00;  /* marker != 0xFFFF */
    assert(zapi_parse(bad_marker, sizeof(bad_marker), &msg) != 0);

    printf("[PASS] test_zapi_invalid: rejects bad input\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_zapi_parse_serialize() != 0) failed++;
    if (test_zapi_decoder() != 0) failed++;
    if (test_zapi_invalid() != 0) failed++;
    printf("=== fib_test (zapi_parse): %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
