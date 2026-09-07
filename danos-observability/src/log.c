/*
 * DANOS-Open Observability: Structured Logging Implementation
 *
 * JSON structured logging to stderr (or syslog in production).
 */

#include <danos/observability/log.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

static char g_component[32] = "danos";
static danos_log_level_t g_min_level = DANOS_LOG_INFO;
static bool g_initialized = false;

static const char *level_name(danos_log_level_t l)
{
    switch (l) {
    case DANOS_LOG_DEBUG: return "debug";
    case DANOS_LOG_INFO:  return "info";
    case DANOS_LOG_WARN:  return "warn";
    case DANOS_LOG_ERROR: return "error";
    case DANOS_LOG_FATAL: return "fatal";
    default:              return "unknown";
    }
}

int danos_log_init(const char *component, danos_log_level_t min_level)
{
    if (component) {
        snprintf(g_component, sizeof(g_component), "%s", component);
    }
    g_min_level = min_level;
    g_initialized = true;
    return 0;
}

void danos_log_set_level(danos_log_level_t level)
{
    g_min_level = level;
}

void danos_log(danos_log_level_t level, const char *msg, ...)
{
    if (level < g_min_level) return;
    if (!msg) return;
    if (!g_initialized) danos_log_init("danos", DANOS_LOG_INFO);

    /* Timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    /* Start JSON output */
    fprintf(stderr, "{\"ts\":%ld.%09ld,\"component\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"",
            (long)ts.tv_sec, (long)ts.tv_nsec,
            g_component, level_name(level), msg);

    /* Key-value pairs: key (const char*) + value (const char*), terminated by NULL key */
    va_list ap;
    va_start(ap, msg);
    const char *key;
    int kv_count = 0;
    while (kv_count < 20) {
        key = va_arg(ap, const char *);
        if (key == NULL) break;
        const char *val = va_arg(ap, const char *);
        if (val) {
            fprintf(stderr, ",\"%s\":\"%s\"", key, val);
        } else {
            fprintf(stderr, ",\"%s\":null", key);
        }
        kv_count++;
    }
    va_end(ap);

    fprintf(stderr, "}\n");
    fflush(stderr);
}
