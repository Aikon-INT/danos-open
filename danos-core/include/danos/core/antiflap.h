/*
 * DANOS-Open Core: Reconciler Anti-Flap (B9)
 *
 * Prevents reconciler oscillation:
 *   - Tracks repair attempts per object within a time window
 *   - If an object is repaired more than max_count times in window_ms,
 *     it is marked as "flapping" and auto-repair is suppressed
 *   - Flapping objects are logged and require manual intervention
 *
 * Default: 5s window, max 3 repairs
 */

#ifndef DANOS_ANTIFLAP_H__
#define DANOS_ANTIFLAP_H__

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Anti-flap configuration */
typedef struct {
    uint32_t window_ms;     /* time window (default 5000) */
    uint32_t max_count;     /* max repairs in window (default 3) */
} antiflap_config_t;

/* Per-object repair history entry */
typedef struct {
    uint64_t obj_key;       /* hash of (type, id) */
    uint64_t timestamps[8]; /* repair timestamps (ring buffer) */
    uint32_t count;         /* number of repairs in current window */
    uint64_t window_start;  /* start of current window (ns) */
    bool     flapping;      /* marked as flapping */
    uint32_t total_repairs; /* total repairs since init */
    uint32_t suppressed;    /* repairs suppressed */
} antiflap_entry_t;

/* Anti-flap context */
typedef struct {
    antiflap_config_t config;
    antiflap_entry_t entries[256];  /* hash table */
    uint32_t entry_count;
    uint64_t total_suppressed;
    pthread_mutex_t lock;
} antiflap_ctx_t;

/* Default config: 5s window, 3 max */
static const antiflap_config_t ANTIFLAP_DEFAULT = {
    .window_ms = 5000,
    .max_count = 3,
};

/* Initialize anti-flap context */
int antiflap_init(antiflap_ctx_t *ctx, const antiflap_config_t *config);

/* Destroy anti-flap context */
void antiflap_fini(antiflap_ctx_t *ctx);

/* Check if a repair should be allowed for an object.
 * Returns:
 *   true  — repair allowed (and recorded)
 *   false — repair suppressed (object is flapping)
 */
bool antiflap_check_and_record(antiflap_ctx_t *ctx, uint64_t obj_key);

/* Check if an object is currently flapping */
bool antiflap_is_flapping(antiflap_ctx_t *ctx, uint64_t obj_key);

/* Clear flapping state for an object (manual intervention) */
int antiflap_clear(antiflap_ctx_t *ctx, uint64_t obj_key);

/* Get statistics */
typedef struct {
    uint32_t tracked_objects;
    uint32_t flapping_objects;
    uint64_t total_suppressed;
} antiflap_stats_t;

void antiflap_get_stats(antiflap_ctx_t *ctx, antiflap_stats_t *out);

/* Get current time in nanoseconds */
uint64_t antiflap_now_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_ANTIFLAP_H__ */
