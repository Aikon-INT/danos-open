/*
 * v0.4 V-group live test: run against a REAL VPP.
 *
 *   1. connect to the VPP binary API socket (env VPP_API_SOCK,
 *      default /run/vpp/api.sock)
 *   2. perform the sockclnt_create handshake, dump message-table size
 *   3. control_ping round trip
 *   4. sw_interface_dump-equivalent: set admin-up on sw_if_index 1 and
 *      report the reply retval (use `vppctl show int` to confirm)
 *   5. stat segment query (env VPP_STAT_SOCK, default /run/vpp/stats.sock)
 *
 * Exit 0 = all checks passed.
 */

#include <danos/dpa.h>
#include "../danos-vpp/src/api/vpp_api.h"
#include <stdint.h>
#include "../danos-vpp/src/api/vpp_msgs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    int failed = 0;
    const char *sock = getenv("VPP_API_SOCK");
    danos_vpp_api_init();
    if (sock) danos_vpp_api_set_sock_path(sock);
    if (danos_vpp_api_connect() != 0) {
        fprintf(stderr, "V1 FAIL: cannot connect to %s\n",
                sock ? sock : "/run/vpp/api.sock");
        return 1;
    }
    uint32_t n = danos_vpp_api_msg_table_count();
    printf("V1 handshake ok: client_index=%u, %u messages in table\n",
           danos_vpp_api_client_index(), n);
    if (n < 100) {
        fprintf(stderr, "V1 FAIL: suspiciously small message table\n");
        failed++;
    }

    if (vpp_msg_control_ping() != DANOS_OK) {
        fprintf(stderr, "V1 FAIL: control_ping round trip\n");
        failed++;
    } else {
        printf("V1 control_ping round trip ok\n");
    }

    danos_status_t st = vpp_msg_sw_interface_set_flags(1, true);
    if (st != DANOS_OK) {
        fprintf(stderr, "V2 FAIL: sw_interface_set_flags(1,up) -> %s\n",
                danos_status_str(st));
        failed++;
    } else {
        printf("V2 sw_interface_set_flags(1,up) accepted "
               "(verify with: vppctl show int)\n");
    }

    /* V3: exercise the real API route path with two equal-cost members. */
    vpp_prefix_t route = { .addr = { .is_ipv6 = false,
                                      .addr = {198, 51, 100, 0} },
                           .len = 24 };
    vpp_ip_t nhs[2] = {
        { .is_ipv6 = false, .addr = {10, 0, 0, 1} },
        { .is_ipv6 = false, .addr = {10, 0, 0, 2} },
    };
    uint32_t nh_ifs[2] = {1, 2};
    st = vpp_msg_ip_route_add_del(true, 0, &route, 2, nhs, nh_ifs);
    if (st != DANOS_OK) {
        fprintf(stderr, "V3 FAIL: ECMP route add -> %s\n", danos_status_str(st));
        failed++;
    } else {
        printf("V3 API ECMP route add accepted (2 paths)\n");
        st = vpp_msg_ip_route_add_del(false, 0, &route, 2, nhs, nh_ifs);
        if (st != DANOS_OK) {
            fprintf(stderr, "V3 FAIL: ECMP route delete -> %s\n", danos_status_str(st));
            failed++;
        } else {
            printf("V3 API ECMP route delete accepted\n");
        }
    }

    if (danos_vpp_api_connect_stat() != 0) {
        fprintf(stderr, "V4 WARN: stat segment connect failed "
                "(check statseg socket-name in startup.conf)\n");
    } else {
        uint64_t v = danos_vpp_api_stat_query("/sys/node/vectors");
        printf("V4 stat /sys/node/vectors = %lu\n", (unsigned long)v);
    }

    danos_vpp_api_disconnect();
    printf("=== vpp_live_test: %s ===\n",
           failed == 0 ? "PASS" : "FAILURES");
    return failed ? 1 : 0;
}
