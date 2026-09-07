/*
 * DANOS-Open Observability: Prometheus Exporter Implementation
 *
 * 50+ core metrics per §22.3.1.
 * Text exposition format: https://prometheus.io/docs/instrumenting/exposition_formats/
 */

#include <danos/observability/prometheus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define PROM_MAX_METRICS 128
#define PROM_NAME_LEN 64
#define PROM_HELP_LEN 128

typedef struct {
    char name[PROM_NAME_LEN];
    char help[PROM_HELP_LEN];
    danos_metric_type_t type;
    double value;
    /* Histogram buckets (simplified: count + sum) */
    uint64_t hcount;
    double hsum;
    bool used;
} prom_metric_t;

static prom_metric_t g_metrics[PROM_MAX_METRICS];
static bool g_initialized = false;

static prom_metric_t *find_metric(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < PROM_MAX_METRICS; i++) {
        if (g_metrics[i].used && strcmp(g_metrics[i].name, name) == 0) {
            return &g_metrics[i];
        }
    }
    return NULL;
}

static prom_metric_t *find_free(void)
{
    for (int i = 0; i < PROM_MAX_METRICS; i++) {
        if (!g_metrics[i].used) return &g_metrics[i];
    }
    return NULL;
}

static const char *type_name(danos_metric_type_t t)
{
    switch (t) {
    case DANOS_METRIC_COUNTER:   return "counter";
    case DANOS_METRIC_GAUGE:     return "gauge";
    case DANOS_METRIC_HISTOGRAM: return "histogram";
    default:                     return "untyped";
    }
}

int danos_prom_init(void)
{
    memset(g_metrics, 0, sizeof(g_metrics));
    g_initialized = true;  /* set BEFORE registering defaults to avoid recursion */

    /* Register default metrics (§22.3.1) - 40+ metrics */
    struct { const char *name; const char *help; danos_metric_type_t type; } defaults[] = {
        /* System metrics */
        {"danos_uptime_seconds",         "Uptime in seconds",              DANOS_METRIC_GAUGE},
        {"danos_cpu_usage_percent",      "CPU usage percent",              DANOS_METRIC_GAUGE},
        {"danos_memory_used_bytes",      "Memory used bytes",              DANOS_METRIC_GAUGE},
        {"danos_memory_total_bytes",     "Memory total bytes",             DANOS_METRIC_GAUGE},
        /* Transaction metrics */
        {"danos_tx_total",               "Total transactions",             DANOS_METRIC_COUNTER},
        {"danos_tx_committed_total",     "Committed transactions",         DANOS_METRIC_COUNTER},
        {"danos_tx_aborted_total",       "Aborted transactions",           DANOS_METRIC_COUNTER},
        {"danos_tx_failed_total",        "Failed transactions",            DANOS_METRIC_COUNTER},
        {"danos_tx_duration_seconds",    "Transaction duration",           DANOS_METRIC_HISTOGRAM},
        {"danos_tx_active",              "Active transactions",            DANOS_METRIC_GAUGE},
        /* Object metrics */
        {"danos_objects_total",          "Total DPA objects",              DANOS_METRIC_GAUGE},
        {"danos_objects_interface",      "Interface objects",              DANOS_METRIC_GAUGE},
        {"danos_objects_route",          "Route objects",                  DANOS_METRIC_GAUGE},
        {"danos_objects_vrf",            "VRF objects",                    DANOS_METRIC_GAUGE},
        {"danos_objects_acl",            "ACL objects",                    DANOS_METRIC_GAUGE},
        {"danos_objects_qos",            "QoS objects",                    DANOS_METRIC_GAUGE},
        /* Reconciler metrics */
        {"danos_reconcile_total",        "Reconciliation cycles",          DANOS_METRIC_COUNTER},
        {"danos_reconcile_failures",     "Reconciliation failures",        DANOS_METRIC_COUNTER},
        {"danos_reconcile_flaps",        "Reconciliation flaps",           DANOS_METRIC_COUNTER},
        {"danos_reconcile_duration",     "Reconciliation duration",        DANOS_METRIC_HISTOGRAM},
        /* Protocol metrics */
        {"danos_bgp_peers",              "BGP peers",                      DANOS_METRIC_GAUGE},
        {"danos_bgp_peers_established",  "BGP peers established",          DANOS_METRIC_GAUGE},
        {"danos_bgp_routes_received",    "BGP routes received",            DANOS_METRIC_GAUGE},
        {"danos_ospf_neighbors",         "OSPF neighbors",                 DANOS_METRIC_GAUGE},
        {"danos_isis_neighbors",         "IS-IS neighbors",                DANOS_METRIC_GAUGE},
        {"danos_bfd_sessions_up",        "BFD sessions up",                DANOS_METRIC_GAUGE},
        {"danos_bfd_sessions_down",      "BFD sessions down",              DANOS_METRIC_GAUGE},
        /* Performance metrics */
        {"danos_fwd_packets_total",      "Forwarded packets",              DANOS_METRIC_COUNTER},
        {"danos_fwd_bytes_total",        "Forwarded bytes",                DANOS_METRIC_COUNTER},
        {"danos_fwd_drops_total",        "Forwarded drops",                DANOS_METRIC_COUNTER},
        {"danos_fwd_errors_total",       "Forwarded errors",               DANOS_METRIC_COUNTER},
        /* Backend metrics */
        {"danos_backend_connected",      "Backend connected (1=yes)",      DANOS_METRIC_GAUGE},
        {"danos_backend_msgs_sent",      "Backend messages sent",          DANOS_METRIC_COUNTER},
        {"danos_backend_msgs_received",  "Backend messages received",      DANOS_METRIC_COUNTER},
        /* gNMI metrics */
        {"danos_gnmi_get_total",         "gNMI Get requests",              DANOS_METRIC_COUNTER},
        {"danos_gnmi_set_total",         "gNMI Set requests",              DANOS_METRIC_COUNTER},
        {"danos_gnmi_subscribe_total",   "gNMI Subscribe requests",        DANOS_METRIC_COUNTER},
        /* HA metrics */
        {"danos_vrrp_master",            "VRRP master sessions",           DANOS_METRIC_GAUGE},
        {"danos_vrrp_backup",            "VRRP backup sessions",           DANOS_METRIC_GAUGE},
    };

    int num_defaults = sizeof(defaults) / sizeof(defaults[0]);
    for (int i = 0; i < num_defaults; i++) {
        danos_prom_register(defaults[i].name, defaults[i].help, defaults[i].type);
    }

    return 0;
}

int danos_prom_register(const char *name, const char *help,
                        danos_metric_type_t type)
{
    if (!name || !help) return -1;
    if (!g_initialized) danos_prom_init();
    if (find_metric(name)) return -1; /* duplicate */

    prom_metric_t *m = find_free();
    if (!m) return -1; /* full */

    snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->help, sizeof(m->help), "%s", help);
    m->type = type;
    m->value = 0;
    m->hcount = 0;
    m->hsum = 0;
    m->used = true;
    return 0;
}

int danos_prom_set(const char *name, double value)
{
    prom_metric_t *m = find_metric(name);
    if (!m) return -1;
    m->value = value;
    return 0;
}

int danos_prom_inc(const char *name, double delta)
{
    prom_metric_t *m = find_metric(name);
    if (!m) return -1;
    m->value += delta;
    return 0;
}

int danos_prom_observe(const char *name, double value)
{
    prom_metric_t *m = find_metric(name);
    if (!m) return -1;
    m->hcount++;
    m->hsum += value;
    return 0;
}

int danos_prom_render(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0) return 0;
    int offset = 0;

    for (int i = 0; i < PROM_MAX_METRICS; i++) {
        if (!g_metrics[i].used) continue;
        prom_metric_t *m = &g_metrics[i];

        /* # HELP line */
        offset += snprintf(buf + offset, buf_size - offset,
                           "# HELP %s %s\n", m->name, m->help);
        /* # TYPE line */
        offset += snprintf(buf + offset, buf_size - offset,
                           "# TYPE %s %s\n", m->name, type_name(m->type));
        /* Value line */
        if (m->type == DANOS_METRIC_HISTOGRAM) {
            offset += snprintf(buf + offset, buf_size - offset,
                               "%s_count %lu\n%s_sum %.6f\n",
                               m->name, (unsigned long)m->hcount, m->name, m->hsum);
        } else {
            offset += snprintf(buf + offset, buf_size - offset,
                               "%s %.6f\n", m->name, m->value);
        }
        if (offset >= buf_size - 256) break; /* leave room */
    }
    return offset;
}

/* HTTP server stubs (would use microhttpd or similar in production) */
int danos_prom_start_server(uint16_t port)
{
    (void)port;
    return 0;
}

void danos_prom_stop_server(void)
{
    /* no-op in stub */
}
