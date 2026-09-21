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

int test_map_frr_route_add(void)
{
    /* Native FRR v6 payload: type=static, instance=0, flags=0,
     * message=0, SAFI unicast, IPv4 10.250.0.0/24, no nexthops. */
    uint8_t payload[] = {9, 0,0, 0,0,0,0, 0,0,0,0, 1,2,24, 10,250,0};
    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_FRR_REDISTRIBUTE_ROUTE_ADD;
    msg.header.version = ZAPI_VERSION;
    msg.vrf_id = 0;
    msg.payload = payload;
    msg.payload_size = sizeof(payload);
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "frr-test", NULL) == DANOS_OK);
    assert(zapi_dispatch_frr(&msg, &tx) == DANOS_OK);
    danos_tx_commit(&tx);
    printf("[PASS] test_map_frr_route_add: native FRR route -> DPA\n");
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

/* ===== v0.2: new mapper tests ===== */

/* Helper: create an interface so update mappers have something to update */
static void create_test_iface(danos_tx_t *tx, uint32_t ifindex,
                              const char *name, uint16_t mtu)
{
    danos_iface_t iface;
    memset(&iface, 0, sizeof(iface));
    iface.ifindex = ifindex;
    iface.type = DANOS_IF_TYPE_PHYS;
    iface.mtu = mtu;
    iface.admin_up = true;
    strncpy(iface.name, name, sizeof(iface.name) - 1);
    danos_iface_create(tx, &iface);
}

int test_map_interface_set_mtu(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Pre-create interface with MTU 1500 */
    create_test_iface(&tx, 5, "eth5", 1500);

    /* Build SET_MTU payload: ifindex=5, mtu=9000 */
    uint8_t payload[8];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 5);      /* ifindex */
    zapi_encode_u32(&enc, 9000);   /* mtu */

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_INTERFACE_SET_MTU;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    /* Verify MTU was updated */
    danos_iface_t iface;
    st = danos_iface_read(&tx, 5, &iface);
    assert(st == DANOS_OK);
    assert(iface.mtu == 9000);

    danos_tx_commit(&tx);
    printf("[PASS] test_map_interface_set_mtu: ZAPI SET_MTU → DPA iface update\n");
    return 0;
}

int test_map_interface_set_mtu_not_found(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* SET_MTU on non-existent interface → NOT_FOUND */
    uint8_t payload[8];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 999);    /* ifindex not created */
    zapi_encode_u32(&enc, 1500);

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_INTERFACE_SET_MTU;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_ERR_NOT_FOUND);

    danos_tx_abort(&tx);
    printf("[PASS] test_map_interface_set_mtu_not_found: returns NOT_FOUND\n");
    return 0;
}

int test_map_interface_up_down(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Pre-create interface with admin_up=true */
    create_test_iface(&tx, 7, "eth7", 1500);

    /* INTERFACE_DOWN: ifindex=7 */
    uint8_t payload[4];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 7);

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_INTERFACE_DOWN;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    danos_iface_t iface;
    st = danos_iface_read(&tx, 7, &iface);
    assert(st == DANOS_OK);
    assert(iface.admin_up == false);

    /* INTERFACE_UP: ifindex=7 */
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 7);
    msg.header.command = ZEBRA_INTERFACE_UP;
    msg.payload_size = enc.pos;

    st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    st = danos_iface_read(&tx, 7, &iface);
    assert(st == DANOS_OK);
    assert(iface.admin_up == true);

    danos_tx_commit(&tx);
    printf("[PASS] test_map_interface_up_down: ZAPI UP/DOWN → DPA admin_up\n");
    return 0;
}

int test_map_redistribute_add(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* REDISTRIBUTE_ADD payload: protocol=2 (bgp) */
    uint8_t payload[] = { 0x02 };

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_REDISTRIBUTE_ADD;
    msg.payload = payload;
    msg.payload_size = sizeof(payload);

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    danos_tx_commit(&tx);
    printf("[PASS] test_map_redistribute_add: acknowledged (no-op)\n");
    return 0;
}

int test_map_nexthop_lookup_not_found(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* NEXTHOP_LOOKUP payload: vrf=0, IPv4, gateway=10.0.0.1 */
    uint8_t payload[9];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 0);      /* vrf_id */
    zapi_encode_u8(&enc, 4);       /* family=IPv4 */
    zapi_encode_bytes(&enc, (uint8_t*)"\x0A\x00\x00\x01", 4);  /* 10.0.0.1 */

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_NEXTHOP_LOOKUP;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    /* No NH created → NOT_FOUND */
    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_ERR_NOT_FOUND);

    danos_tx_abort(&tx);
    printf("[PASS] test_map_nexthop_lookup_not_found: returns NOT_FOUND\n");
    return 0;
}

int test_map_labels_add_delete(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Build LABELS_ADD payload:
     * in_label=100, type=STATIC(4), nhgroup_id=1, php=true,
     * push_label_count=2, push_labels=[200, 300] */
    uint8_t payload[32];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 100);     /* in_label */
    zapi_encode_u8(&enc, 4);        /* type=STATIC */
    zapi_encode_u64(&enc, 1);       /* nhgroup_id */
    zapi_encode_u8(&enc, 1);        /* php=true */
    zapi_encode_u8(&enc, 2);        /* push_label_count=2 */
    zapi_encode_u32(&enc, 200);     /* push_labels[0] */
    zapi_encode_u32(&enc, 300);     /* push_labels[1] */

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_LABELS_ADD;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    danos_status_t st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    /* Verify LSP was created */
    danos_mpls_lsp_t lsp;
    st = danos_mpls_lsp_read(&tx, 100, &lsp);
    assert(st == DANOS_OK);
    assert(lsp.in_label == 100);
    assert(lsp.type == DANOS_MPLS_TYPE_STATIC);
    assert(lsp.nhgroup_id == 1);
    assert(lsp.php == true);
    assert(lsp.push_label_count == 2);
    assert(lsp.push_labels[0] == 200);
    assert(lsp.push_labels[1] == 300);

    /* LABELS_DELETE: in_label=100 */
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 100);     /* in_label */
    zapi_encode_u8(&enc, 4);        /* type */
    zapi_encode_u64(&enc, 1);       /* nhgroup_id */
    zapi_encode_u8(&enc, 1);        /* php */
    zapi_encode_u8(&enc, 0);        /* push_label_count=0 */
    msg.header.command = ZEBRA_LABELS_DELETE;
    msg.payload_size = enc.pos;

    st = zapi_dispatch(&msg, &tx);
    assert(st == DANOS_OK);

    /* Verify LSP was deleted */
    st = danos_mpls_lsp_read(&tx, 100, &lsp);
    assert(st == DANOS_ERR_NOT_FOUND);

    danos_tx_commit(&tx);
    printf("[PASS] test_map_labels_add_delete: ZAPI LABELS_ADD/DELETE → DPA MPLS LSP\n");
    return 0;
}

int test_map_route_ecmp(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test-ecmp", NULL) == DANOS_OK);
    uint8_t payload[128];
    zapi_encoder_t enc; zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 7); zapi_encode_u8(&enc, 4); zapi_encode_u8(&enc, 24);
    uint8_t prefix[4] = {10, 77, 0, 0}; zapi_encode_bytes(&enc, prefix, 4);
    zapi_encode_u8(&enc, 2); zapi_encode_u8(&enc, 20); zapi_encode_u32(&enc, 10);
    zapi_encode_u8(&enc, 2);
    zapi_encode_u8(&enc, 1); uint8_t gw1[4] = {192,0,2,1};
    zapi_encode_bytes(&enc, gw1, 4); zapi_encode_u32(&enc, 11);
    zapi_encode_u8(&enc, 1); uint8_t gw2[4] = {192,0,2,2};
    zapi_encode_bytes(&enc, gw2, 4); zapi_encode_u32(&enc, 12);
    zapi_message_t msg; memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_ROUTE_ADD; msg.payload = payload; msg.payload_size = enc.pos;
    assert(zapi_dispatch(&msg, &tx) == DANOS_OK);
    danos_nhgroup_t grp; assert(danos_nhgroup_read(&tx, 1000, &grp) == DANOS_OK);
    assert(grp.nh_count == 2);
    danos_nexthop_t nh; assert(danos_nh_read(&tx, grp.nh_ids[1], &nh) == DANOS_OK);
    assert(nh.ifindex == 12 && (nh.flags & DANOS_NH_FLAG_ECMP));
    danos_tx_commit(&tx);
    printf("[PASS] test_map_route_ecmp: ZAPI multipath -> DPA NHGroup\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_map_route_add() != 0) failed++;
    if (test_map_frr_route_add() != 0) failed++;
    if (test_map_interface_add() != 0) failed++;
    if (test_map_unsupported() != 0) failed++;
    /* v0.2 new mappers */
    if (test_map_interface_set_mtu() != 0) failed++;
    if (test_map_interface_set_mtu_not_found() != 0) failed++;
    if (test_map_interface_up_down() != 0) failed++;
    if (test_map_redistribute_add() != 0) failed++;
    if (test_map_nexthop_lookup_not_found() != 0) failed++;
    if (test_map_labels_add_delete() != 0) failed++;
    if (test_map_route_ecmp() != 0) failed++;
    printf("=== fib_test (zapi_mapper): %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
