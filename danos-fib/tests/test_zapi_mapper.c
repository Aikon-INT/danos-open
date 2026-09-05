/*
 * Test: ZAPI → DPA Mapper (C2)
 * Verify ZAPI messages are correctly mapped to DPA operations.
 */
#include "../src/zapi/zapi.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Build a ZAPI ROUTE_ADD message and dispatch it */
int test_map_route_add(void)
{
    /* Ensure tx manager is initialized */
    danos_tx_t tx = {0};
    danos_status_t st = danos_tx_begin(&tx, "test", NULL);
    assert(st == DANOS_OK);

    /* Build route payload: vrf=0, IPv4, /24, 10.0.0.0, static, AD=1, metric=0, 0 NH */
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,  /* vrf_id=0 */
        0x04,                      /* family=IPv4 */
        0x18,                      /* prefix_len=24 */
        0x0A, 0x00, 0x00, 0x00,   /* 10.0.0.0 */
        0x01,                      /* proto=static */
        0x01,                      /* admin_dist=1 */
        0x00, 0x00, 0x00, 0x00,   /* metric=0 */
        0x00                       /* nh_count=0 */
    };

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_ROUTE_ADD;
    msg.payload = payload;
    msg.payload_size = sizeof(payload);

    st = zapi_dispatch(&msg, &tx);
    /* Should succeed (route created in DPA store) */
    assert(st == DANOS_OK);

    danos_tx_commit(&tx);
    printf("[PASS] test_map_route_add: ZAPI ROUTE_ADD → DPA route\n");
    return 0;
}

int test_map_interface_add(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Build interface payload: ifindex=1, name="eth0", mtu=1500 */
    uint8_t payload[64];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 1);              /* ifindex */
    zapi_encode_u8(&enc, 4);               /* name_len */
    zapi_encode_bytes(&enc, (uint8_t*)"eth0", 4);
    zapi_encode_u32(&enc, 1500);           /* mtu */
    zapi_encode_bytes(&enc, (uint8_t*)"\x02\x00\x00\x00\x00\x01", 6);  /* mac */

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_INTERFACE_ADD;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    /* Verify interface was created */
    danos_iface_t iface;
    st = danos_iface_read(&tx, 1, &iface);
    assert(st == DANOS_OK);
    assert(iface.ifindex == 1);
    assert(strcmp(iface.name, "eth0") == 0);
    assert(iface.mtu == 1500);

    danos_tx_commit(&tx);
    printf("[PASS] test_map_interface_add: ZAPI IF_ADD → DPA interface\n");
    return 0;
}

int test_map_unsupported(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = 999;  /* unsupported */
    msg.payload = NULL;
    msg.payload_size = 0;

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_ERR_NOT_SUPPORTED);

    danos_tx_abort(&tx);
    printf("[PASS] test_map_unsupported: returns NOT_SUPPORTED\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_map_route_add() != 0) failed++;
    if (test_map_interface_add() != 0) failed++;
    if (test_map_unsupported() != 0) failed++;
    printf("=== fib_test (zapi_mapper): %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
