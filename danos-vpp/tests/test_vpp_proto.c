/*
 * Test: VPP protocol conformance (v0.3)
 *
 * Exercises the REAL (non-mock) client code path against an in-process
 * mock VPP server that implements the wire protocol:
 *   - sockclnt_create handshake and name->msg_id table
 *   - request/reply transaction with context correlation
 *   - typed message wire layout (ip_route_add_del etc.)
 *   - stat segment: SCM_RIGHTS + mmap + directory walk
 */

#include "../src/api/vpp_api.h"
#include "../src/api/vpp_wire.h"
#include "../src/api/vpp_msgs.h"
#include "mock_vpp_server.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define API_SOCK  "/tmp/danos-test-vpp-api.sock"
#define STAT_SOCK "/tmp/danos-test-vpp-stat.sock"

static int test_handshake(void)
{
    assert(mock_vpp_start(API_SOCK) == 0);
    danos_vpp_api_init();
    danos_vpp_api_set_sock_path(API_SOCK);

    assert(danos_vpp_api_connect() == 0);
    assert(danos_vpp_api_is_connected());
    assert(danos_vpp_api_client_index() == 0x1234);
    assert(danos_vpp_api_msg_table_count() == 5);

    uint16_t id;
    assert(danos_vpp_api_lookup_msg_id("control_ping", &id));
    assert(id == MOCK_MSGID_CONTROL_PING);
    assert(danos_vpp_api_lookup_msg_id("ip_route_add_del", &id));
    assert(id == MOCK_MSGID_IP_ROUTE_ADD_DEL);
    assert(!danos_vpp_api_lookup_msg_id("nonexistent_message_xyz", &id));

    danos_vpp_api_disconnect();
    mock_vpp_stop();
    printf("[PASS] test_handshake: sockclnt_create + msg table\n");
    return 0;
}

static int test_transaction(void)
{
    assert(mock_vpp_start(API_SOCK) == 0);
    danos_vpp_api_init();
    danos_vpp_api_set_sock_path(API_SOCK);
    assert(danos_vpp_api_connect() == 0);

    /* control_ping: no body; reply retval 0 */
    assert(vpp_msg_control_ping() == DANOS_OK);

    /* typed message through the real socket */
    assert(vpp_msg_sw_interface_set_flags(3, true) == DANOS_OK);
    uint16_t mid;
    uint8_t body[64];
    uint32_t blen;
    mock_vpp_get_last_request(&mid, body, &blen, sizeof(body));
    assert(mid == MOCK_MSGID_SW_IF_SET_FLAGS);
    /* body = client_index(4) + context(4) + sw_if_index(4) + flags(4) BE */
    assert(blen == 16);
    assert(body[8] == 0 && body[9] == 0 && body[10] == 0 && body[11] == 3);
    assert(body[12] == 0 && body[15] == 1);

    danos_vpp_api_disconnect();
    mock_vpp_stop();
    printf("[PASS] test_transaction: request/reply + context correlation\n");
    return 0;
}

static int test_wire_layouts(void)
{
    /* ip_table_add_del: is_add(1) + table_id(4) + is_ip6(1) + name(u8len+n) */
    uint8_t b[512];
    int n = vpp_encode_ip_table_add_del(1, 100, false, "danos-vrf100",
                                        b, sizeof(b));
    assert(n == 1 + 4 + 1 + 1 + 12);
    assert(b[0] == 1);
    assert(b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 100);
    assert(b[5] == 0);
    assert(b[6] == 12 && memcmp(b + 7, "danos-vrf100", 12) == 0);

    /* ip_route_add_del single path IPv4 */
    vpp_prefix_t p = { .addr = { .is_ipv6 = false, .addr = {10,0,0,0} },
                       .len = 24 };
    vpp_ip_t nh = { .is_ipv6 = false, .addr = {192,168,1,1} };
    uint32_t nhif = 1;
    n = vpp_encode_ip_route_add_del(1, 0, &p, 1, &nh, &nhif, b, sizeof(b));
    assert(n > 0);
    /* is_add, is_multipath, table_id(4), stats_index(4), prefix(18), n_paths */
    assert(b[0] == 1 && b[1] == 0);
    assert(b[2] == 0 && b[5] == 0);      /* table_id = 0 */
    assert(b[10] == 0);                  /* prefix af = IP4 */
    assert(b[27] == 24);                 /* prefix len */
    assert(b[28] == 1);                  /* n_paths */
    /* path: sw_if_index(4) table_id(4) rpf_id(4) w(1) pref(1) type(1)
     * flags(1) proto(1) nh(16) n_labels(1) labels(112) */
    int off = 29;
    assert(b[off + 3] == 1);             /* sw_if_index = 1 */
    assert(b[off + 12] == 1);            /* weight = 1 */
    assert(b[off + 16] == 0);            /* nh proto ip4 */
    assert(b[off + 17] == 192);          /* nh addr first byte */

    /* sw_interface_add_del_address: index + is_add + del_all + address + prefix len */
    vpp_prefix_t ap = { .addr = { .is_ipv6 = false, .addr = {192,0,2,1} },
                        .len = 24 };
    n = vpp_encode_sw_interface_add_del_address(7, true, &ap, b, sizeof(b));
    assert(n == 24);
    assert(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 7);
    assert(b[4] == 1 && b[5] == 0 && b[6] == 0);
    assert(b[7] == 192 && b[8] == 0 && b[9] == 2 && b[10] == 1);
    assert(b[23] == 24);

    /* neighbor */
    uint8_t mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    vpp_ip_t ip6nh = { .is_ipv6 = true };
    ip6nh.addr[0] = 0xfe; ip6nh.addr[1] = 0x80;
    n = vpp_encode_ip_neighbor_add_del(1, 2, mac, &ip6nh, b, sizeof(b));
    assert(n > 0);
    assert(b[0] == 1);
    assert(b[1] == 0);                   /* is_del_all */
    assert(b[7] == 0xaa);                /* mac[0] after hdr */
    assert(b[13] == 1);                  /* af = IP6 */

    /* policer_add_del (CoPP): is_add(1) + name(u8len+5) + cir(4) + eir(4)
     * + cb(8) + eb(8) + rate(1) + round(1) + type(1) + color(1) + 3*action(2) */
    uint8_t pb[128];
    n = vpp_encode_policer_add_del(1, "cop10", 1000000, 2000000, 16000, 32000,
                                   2, 46, 1, 0, 0, 0, pb, sizeof(pb));
    assert(n == 1 + 1 + 5 + 4 + 4 + 8 + 8 + 1 + 1 + 1 + 1 + 6);
    assert(pb[0] == 1);                       /* is_add */
    assert(pb[1] == 5);                       /* name len */
    assert(memcmp(pb + 2, "cop10", 5) == 0);
    /* cir = 1000000 KBPS big-endian at offset 7 (after is_add+len+name) */
    assert(pb[7] == 0x00 && pb[8] == 0x0F && pb[9] == 0x42 && pb[10] == 0x40);
    /* type = 2R3C RFC2698 (eir > 0): offset 7+4+4+8+8+2 = 33 */
    assert(pb[33] == 2);
    printf("[PASS] test_wire_layouts: vl_api struct encoding + policer\n");
    return 0;
}

static int test_stat_segment(void)
{
    assert(mock_statseg_start(STAT_SOCK) == 0);
    danos_vpp_api_init();
    danos_vpp_api_set_stat_sock_path(STAT_SOCK);
    assert(danos_vpp_api_connect_stat() == 0);

    /* scalar counter */
    assert(danos_vpp_api_stat_query("/sys/node/ip4-input") == 1000000);
    /* simple counter summed over 2 workers: 3000 + 2000 */
    assert(danos_vpp_api_stat_query("/if/0/rx-packets") == 5000);
    /* unknown counter */
    assert(danos_vpp_api_stat_query("/no/such/counter") == 0);

    /* directory listing */
    const char *names[16];
    uint64_t values[16];
    int count = danos_vpp_api_stat_list(names, values, 16);
    assert(count == 2);

    printf("[PASS] test_stat_segment: SCM_RIGHTS + mmap + directory walk\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_handshake() != 0) failed++;
    if (test_transaction() != 0) failed++;
    if (test_wire_layouts() != 0) failed++;
    if (test_stat_segment() != 0) failed++;
    mock_statseg_stop();
    printf("=== vpp_proto_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
