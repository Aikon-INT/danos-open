/*
 * Live FRR zebra ZAPI -> DANOS FIB/DPA -> VPP bridge.
 *
 * The daemon processes one transaction per zebra message.  It is bounded by
 * --messages for deterministic acceptance runs, and otherwise runs until
 * zebra closes the socket.  No mock protocol or alternate route mapper is
 * used here: the session, parser, mapper, DPA transaction, and VPP adapter
 * are the same production components used by the eventual daemon.
 */
#include "../danos-fib/src/zapi/zapi.h"
#include "../danos-vpp/src/vpp_adapter.h"
#include <danos/core/backend_ops.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void usage(const char *p)
{
    fprintf(stderr, "usage: %s --zebra-sock PATH --vpp-sock PATH [--messages N]\n", p);
}

int main(int argc, char **argv)
{
    const char *zebra = getenv("DANOS_ZEBRA_SOCK");
    const char *vpp = getenv("DANOS_VPP_API_SOCK");
    long limit = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--zebra-sock") && i + 1 < argc) zebra = argv[++i];
        else if (!strcmp(argv[i], "--vpp-sock") && i + 1 < argc) vpp = argv[++i];
        else if (!strcmp(argv[i], "--messages") && i + 1 < argc) limit = strtol(argv[++i], NULL, 10);
        else { usage(argv[0]); return 2; }
    }
    if (!zebra || !vpp || (limit < 0)) { usage(argv[0]); return 2; }
    if (danos_zebra_session_init() != 0 || danos_zebra_session_set_socket(zebra) != 0) {
        fprintf(stderr, "zebra session setup failed\n"); return 1;
    }
    if (danos_vpp_adapter_install(true, vpp) != 0) {
        fprintf(stderr, "VPP adapter setup failed: %s\n", strerror(errno)); return 1;
    }
    if (danos_zebra_session_connect() != 0) {
        fprintf(stderr, "cannot connect zebra socket %s: %s\n", zebra, strerror(errno)); return 1;
    }

    uint8_t buf[128 * 1024];
    long processed = 0;
    int failed = 0;
    for (;;) {
        zapi_message_t msg;
        int n = danos_zebra_session_recv(buf, sizeof(buf), &msg);
        if (n == 0) break;
        if (n < 0) { fprintf(stderr, "zebra receive failed: %d\n", n); failed++; break; }
        danos_tx_t tx = {0};
        danos_status_t st = danos_tx_begin(&tx, "frr-zebra", NULL);
        if (st == DANOS_OK) st = zapi_dispatch(&msg, &tx);
        if (st == DANOS_OK) st = danos_tx_commit_atomic(&tx);
        if (st != DANOS_OK) {
            fprintf(stderr, "ZAPI command %u transaction failed: %s\n",
                    msg.header.command, danos_status_str(st));
            failed++;
        } else {
            uint64_t attempted = 0, programming_failed = 0;
            (void)danos_programming_run(&attempted, &programming_failed);
            if (programming_failed != 0) failed++;
            processed++;
        }
        if (limit > 0 && processed >= limit) break;
    }
    danos_zebra_session_disconnect();
    printf("fib_live_bridge: processed=%ld failed=%d\n", processed, failed);
    return (processed > 0 && failed == 0) ? 0 : 1;
}
