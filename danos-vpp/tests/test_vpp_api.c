/*
 * Test: VPP Binary API Client (D1)
 * Verify mock mode operation.
 */
#include "../src/api/vpp_api.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int test_mock_mode(void)
{
    danos_vpp_api_init();
    danos_vpp_api_enable_mock();

    assert(danos_vpp_api_is_connected() == true);

    /* Send a message */
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    assert(danos_vpp_api_send(100, payload, sizeof(payload)) == 0);

    /* Verify last mock message */
    uint16_t msg_id;
    uint32_t msg_size;
    danos_vpp_api_get_last_mock_msg(&msg_id, &msg_size);
    assert(msg_id == 100);
    assert(msg_size == sizeof(payload));

    /* Receive (mock returns 6-byte header) */
    uint8_t buf[64];
    int n = danos_vpp_api_recv(buf, sizeof(buf));
    assert(n == 6);

    /* Stats */
    uint64_t sent, received, connects, reconnects;
    danos_vpp_api_get_stats(&sent, &received, &connects, &reconnects);
    assert(sent == 1);
    assert(received == 1);

    printf("[PASS] test_mock_mode: mock send/recv works\n");
    return 0;
}

int test_stat_query(void)
{
    danos_vpp_api_init();
    danos_vpp_api_enable_mock();

    /* Mock stat query returns hash of name */
    uint64_t v1 = danos_vpp_api_stat_query("/if/0/rx-packets");
    uint64_t v2 = danos_vpp_api_stat_query("/if/0/rx-packets");
    uint64_t v3 = danos_vpp_api_stat_query("/if/0/tx-packets");
    assert(v1 == v2);   /* same name → same value */
    assert(v1 != v3);   /* different name → different value */
    assert(v1 != 0);

    /* NULL name */
    assert(danos_vpp_api_stat_query(NULL) == 0);

    printf("[PASS] test_stat_query: stat segment query works\n");
    return 0;
}

int test_disconnect(void)
{
    danos_vpp_api_init();
    danos_vpp_api_enable_mock();
    assert(danos_vpp_api_is_connected() == true);

    danos_vpp_api_disconnect();
    assert(danos_vpp_api_is_connected() == false);

    printf("[PASS] test_disconnect: disconnect works\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_mock_mode() != 0) failed++;
    if (test_stat_query() != 0) failed++;
    if (test_disconnect() != 0) failed++;
    printf("=== vpp_api_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
