/*
 * DANOS-Open Observability: Structured Logging Interface
 *
 * Structured JSON logging for all DANOS components.
 * Design: v1.1/observability/observability_telemetry.md §22.2
 */

#ifndef DANOS_OBS_LOG_H__
#define DANOS_OBS_LOG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DANOS_LOG_DEBUG = 0,
    DANOS_LOG_INFO  = 1,
    DANOS_LOG_WARN  = 2,
    DANOS_LOG_ERROR = 3,
    DANOS_LOG_FATAL = 4,
} danos_log_level_t;

/* Initialize structured logging.
 * component: component name (e.g. "danos-core", "danos-vpp")
 * min_level: minimum level to emit
 * Returns 0 on success. */
int danos_log_init(const char *component, danos_log_level_t min_level);

/* Log a message with key-value pairs.
 * Usage: danos_log(DANOS_LOG_INFO, "transaction committed",
 *                  "tx_id", 12345, "duration_ms", 42, NULL);
 * The variadic args are key-value pairs, terminated by NULL. */
void danos_log(danos_log_level_t level, const char *msg, ...);

/* Convenience macros */
#define danos_log_info(msg, ...)  danos_log(DANOS_LOG_INFO,  msg, ##__VA_ARGS__, NULL)
#define danos_log_warn(msg, ...)  danos_log(DANOS_LOG_WARN,  msg, ##__VA_ARGS__, NULL)
#define danos_log_error(msg, ...) danos_log(DANOS_LOG_ERROR, msg, ##__VA_ARGS__, NULL)

/* Set minimum log level at runtime. */
void danos_log_set_level(danos_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_OBS_LOG_H__ */
