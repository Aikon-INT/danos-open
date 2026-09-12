/*
 * DANOS-Open Observability: Prometheus Exporter Interface
 *
 * Exposes 50+ core metrics for Prometheus scraping.
 * Design: v1.1/observability/observability_telemetry.md §22.3
 */

#ifndef DANOS_OBS_PROMETHEUS_H__
#define DANOS_OBS_PROMETHEUS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Metric types */
typedef enum {
    DANOS_METRIC_COUNTER = 1,
    DANOS_METRIC_GAUGE   = 2,
    DANOS_METRIC_HISTOGRAM = 3,
} danos_metric_type_t;

/* Register a metric.
 * name: metric name (e.g. "danos_tx_total")
 * help: help text
 * type: COUNTER/GAUGE/HISTOGRAM
 * Returns 0 on success, -1 on duplicate or full. */
int danos_prom_register(const char *name, const char *help,
                        danos_metric_type_t type);

/* Set metric value (for GAUGE) or increment (for COUNTER). */
int danos_prom_set(const char *name, double value);
int danos_prom_inc(const char *name, double delta);

/* Observe a value for histogram. */
int danos_prom_observe(const char *name, double value);

/* Render all metrics in Prometheus text exposition format.
 * buf: output buffer
 * buf_size: buffer size
 * Returns bytes written. */
int danos_prom_render(char *buf, int buf_size);

/* Start HTTP server on port (default 9100).
 * Returns 0 on success. */
int danos_prom_start_server(uint16_t port);

/* Stop HTTP server. */
void danos_prom_stop_server(void);

/* Initialize with default metrics (§22.3.1). */
int danos_prom_init(void);

/* ---- v0.6: stat providers + real HTTP server -------------------------- */

/* A provider resolves an external counter by name (e.g. the VPP stat
 * segment). Returns the counter value, 0 if unknown. */
typedef uint64_t (*danos_stat_provider_fn)(const char *name);

/* Register the (single) external stat provider. */
void danos_prom_set_stat_provider(danos_stat_provider_fn fn);

/* Bind a metric to an external counter; refreshed at render time via
 * the registered provider. */
int danos_prom_bind_stat(const char *metric, const char *stat_name);

/* Refresh all bound metrics from the provider (called automatically at
 * render). Returns the number of refreshed metrics. */
int danos_prom_refresh(void);

/* Serve GET /metrics over TCP (blocking accept loop in a detached
 * thread). Returns 0 on success. */
int danos_prom_start_server(uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_OBS_PROMETHEUS_H__ */
