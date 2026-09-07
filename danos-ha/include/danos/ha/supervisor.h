/*
 * DANOS-Open HA: Process Supervisor Interface
 *
 * Monitor and restart critical processes with exponential backoff.
 * Design: v1.1/ha/ha_design.md §21.5
 */

#ifndef DANOS_HA_SUPERVISOR_H__
#define DANOS_HA_SUPERVISOR_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     name[64];       /* process name */
    char     cmdline[256];   /* command line */
    char     pidfile[128];   /* PID file path */
    uint32_t restart_min_ms; /* initial restart delay (1s) */
    uint32_t restart_max_ms; /* max restart delay (60s) */
    bool     critical;       /* critical: if cannot restart → safe mode */
} danos_sup_proc_t;

typedef enum {
    DANOS_SUP_STATE_STOPPED   = 0,
    DANOS_SUP_STATE_RUNNING   = 1,
    DANOS_SUP_STATE_RESTARTING = 2,
    DANOS_SUP_STATE_FAILED    = 3, /* exceeded max retries */
    DANOS_SUP_STATE_SAFE_MODE = 4, /* critical failure */
} danos_sup_state_t;

/* Register a process for supervision */
int danos_sup_register(const danos_sup_proc_t *proc);

/* Unregister */
int danos_sup_unregister(const char *name);

/* Get process state */
danos_sup_state_t danos_sup_get_state(const char *name);

/* Get restart count */
uint32_t danos_sup_get_restart_count(const char *name);

/* Start supervision (fork+exec all registered processes) */
int danos_sup_start(void);

/* Stop all supervised processes */
int danos_sup_stop(void);

/* Initialize supervisor */
int danos_sup_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_HA_SUPERVISOR_H__ */
