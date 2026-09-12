/*
 * DANOS-Open Management Daemon (danos-mgrd, v0.6)
 *
 * The system process: wires config persistence, the DPA object store,
 * the gNMI gRPC server, the VPP backend and Prometheus exposition into
 * one long-running control plane.
 *
 * Lifecycle:
 *   1. danos_prom_init + stat provider binding (VPP or mock counters)
 *   2. danos_persist_enable + recover  (boot with last durable config)
 *   3. optional backend connect (VPP when reachable)
 *   4. gNMI gRPC server (management entry point)
 *   5. Prometheus /metrics HTTP server
 *   6. pause until SIGINT/SIGTERM (config survives restarts)
 *
 * Usage:
 *   danos-mgrd [--port N] [--metrics-port N] [--wal PATH]
 *              [--seed] [--vpp-sock PATH] [--no-vpp]
 */

#include <danos/core/persist.h>
#include <danos/core/object_registry.h>
#include <danos/observability/prometheus.h>
#include <danos/dpa.h>
#include "../danos-mgmt/src/gnmi/gnmi_grpc.h"
#include "../danos-vpp/src/api/vpp_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static uint64_t stat_provider(const char *name)
{
    /* v0.6: routed through the VPP stat client (real segment when
     * connected, mock counters otherwise — mock falls back to a
     * deterministic hash so scrapes stay meaningful in tests). */
    return danos_vpp_api_stat_query(name);
}

static void seed_initial_config(void)
{
    /* deterministic bootstrap config; survives via the WAL afterwards */
    danos_tx_t tx;
    if (danos_tx_begin(&tx, "mgrd-seed", NULL) != DANOS_OK) return;

    danos_iface_t ifaces[] = {
        { .ifindex = 1, .name = "eth0", .mtu = 1500, .admin_up = true },
        { .ifindex = 2, .name = "eth1", .mtu = 9000, .admin_up = true },
    };
    for (size_t i = 0; i < sizeof(ifaces) / sizeof(ifaces[0]); i++)
        danos_iface_create(&tx, &ifaces[i]);

    danos_vrf_t mgmt = { .vrf_id = 1, .name = "mgmt", .ipv4_active = true };
    danos_vrf_create(&tx, &mgmt);

    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);
}

static void bind_vpp_metrics(void)
{
    danos_prom_bind_stat("danos_fwd_packets_total", "/sys/node/vectors");
    danos_prom_bind_stat("danos_fwd_errors_total", "/err/ip4-input");
    /* stat names verified against the VPP stat directory; unknown names
     * render as 0 until VPP publishes them. */
}

int main(int argc, char **argv)
{
    uint16_t gnmi_port = 59200;
    uint16_t metrics_port = 59201;
    const char *wal = "/var/lib/danos/mgrd.wal";
    const char *vpp_sock = NULL;
    bool seed = false, use_vpp = true;

    static struct option opts[] = {
        {"port",        required_argument, 0, 'p'},
        {"metrics-port",required_argument, 0, 'm'},
        {"wal",         required_argument, 0, 'w'},
        {"seed",        no_argument,       0, 's'},
        {"vpp-sock",    required_argument, 0, 'v'},
        {"no-vpp",      no_argument,       0, 'n'},
        {0, 0, 0, 0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "p:m:w:sv:n", opts, NULL)) != -1) {
        switch (c) {
        case 'p': gnmi_port = (uint16_t)atoi(optarg); break;
        case 'm': metrics_port = (uint16_t)atoi(optarg); break;
        case 'w': wal = optarg; break;
        case 's': seed = true; break;
        case 'v': vpp_sock = optarg; break;
        case 'n': use_vpp = false; break;
        default:
            fprintf(stderr, "usage: mgrd [--port N] [--metrics-port N] "
                    "[--wal PATH] [--seed] [--vpp-sock PATH] [--no-vpp]\n");
            return 2;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    /* 1. observability */
    danos_prom_init();
    danos_vpp_api_init();
    danos_prom_set_stat_provider(stat_provider);
    bind_vpp_metrics();
    if (use_vpp) {
        if (vpp_sock) danos_vpp_api_set_sock_path(vpp_sock);
        if (danos_vpp_api_connect() == 0) {
            danos_vpp_api_connect_stat();
            printf("mgrd: VPP backend connected (%u api messages, "
                   "stat segment mapped)\n",
                   danos_vpp_api_msg_table_count());
        } else {
            printf("mgrd: VPP not reachable (%s) — running without "
                   "dataplane backend\n",
                   vpp_sock ? vpp_sock : "/run/vpp/api.sock");
        }
    }

    /* 2. persistence: boot with the last durable configuration */
    if (danos_persist_enable(wal) != 0) {
        fprintf(stderr, "mgrd: cannot open WAL %s\n", wal);
        return 1;
    }
    int recovered = danos_persist_recover();
    printf("mgrd: persistence %s (%d records recovered)\n", wal, recovered);
    if (recovered == 0 && seed) {
        seed_initial_config();
        printf("mgrd: seeded bootstrap configuration\n");
    }
    danos_prom_set("danos_objects_interface",
                   (double)danos_object_count(g_default_store,
                                              DANOS_OBJ_IFACE));

    /* 3. northbound */
    danos_gnmi_grpc_ctx_t gnmi;
    danos_gnmi_grpc_init(&gnmi, gnmi_port);
    if (danos_gnmi_grpc_start(&gnmi) != 0) {
        fprintf(stderr, "mgrd: gNMI gRPC server failed on :%u\n", gnmi_port);
        return 1;
    }
    printf("mgrd: gNMI (h2c) on :%u\n", gnmi_port);

    /* 4. observability server */
    if (danos_prom_start_server(metrics_port) != 0) {
        fprintf(stderr, "mgrd: metrics server failed on :%u\n", metrics_port);
        return 1;
    }
    printf("mgrd: prometheus /metrics on :%u\n", metrics_port);
    fflush(stdout);

    while (!g_stop) pause();

    printf("mgrd: shutting down (config persists in %s)\n", wal);
    danos_gnmi_grpc_stop(&gnmi);
    danos_prom_stop_server();
    danos_vpp_api_disconnect();
    danos_persist_disable();
    return 0;
}
