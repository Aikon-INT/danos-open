/*
 * DANOS-Open gNMI demo server: standalone process for external-client
 * interop testing (gnmic, grpc-go clients).
 *
 * Usage: gnmi_demo_server [port]
 * Seeds a couple of objects so Get/Subscribe have data, then serves
 * until killed.
 */

#include <danos/core/persist.h>
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include "../danos-mgmt/src/gnmi/gnmi_grpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static danos_gnmi_grpc_ctx_t g_ctx;

static void on_sig(int sig)
{
    (void)sig;
    danos_gnmi_grpc_stop(&g_ctx);
    exit(0);
}

static void seed(void)
{
    danos_tx_t tx;
    if (danos_tx_begin(&tx, "seed", NULL) != DANOS_OK) return;

    danos_iface_t ifaces[] = {
        { .ifindex = 1, .name = "eth0", .mtu = 1500, .admin_up = true },
        { .ifindex = 2, .name = "eth1", .mtu = 9000, .admin_up = true },
    };
    for (size_t i = 0; i < sizeof(ifaces) / sizeof(ifaces[0]); i++) {
        if (danos_iface_create(&tx, &ifaces[i]) != DANOS_OK) continue;
    }

    danos_vrf_t mgmt = { .vrf_id = 1, .name = "mgmt", .ipv4_active = true };
    danos_vrf_create(&tx, &mgmt);

    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);
}

int main(int argc, char **argv)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : 59200;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    seed();

    danos_gnmi_grpc_init(&g_ctx, port);
    if (danos_gnmi_grpc_start(&g_ctx) != 0) {
        fprintf(stderr, "failed to start gNMI gRPC server on port %u\n", port);
        return 1;
    }
    printf("DANOS-Open gNMI server listening on :%u (h2c, insecure)\n", port);
    fflush(stdout);

    for (;;) pause();
    return 0;
}
