/*
 * DANOS-Open HA: Process Supervisor Implementation
 *
 * Process monitoring with exponential backoff restart.
 */

#include <danos/ha/supervisor.h>
#include <string.h>
#include <stdlib.h>

#define SUP_MAX_PROCS 32

typedef struct {
    danos_sup_proc_t  proc;
    danos_sup_state_t state;
    uint32_t          restart_count;
    uint32_t          current_delay_ms;
    bool              used;
} sup_entry_t;

static sup_entry_t g_procs[SUP_MAX_PROCS];
static bool g_initialized = false;

static sup_entry_t *find_proc(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < SUP_MAX_PROCS; i++) {
        if (g_procs[i].used && strcmp(g_procs[i].proc.name, name) == 0) {
            return &g_procs[i];
        }
    }
    return NULL;
}

static sup_entry_t *find_free(void)
{
    for (int i = 0; i < SUP_MAX_PROCS; i++) {
        if (!g_procs[i].used) return &g_procs[i];
    }
    return NULL;
}

int danos_sup_init(void)
{
    memset(g_procs, 0, sizeof(g_procs));
    g_initialized = true;
    return 0;
}

int danos_sup_register(const danos_sup_proc_t *proc)
{
    if (!proc) return -1;
    if (!g_initialized) danos_sup_init();
    if (find_proc(proc->name)) return -1; /* duplicate */

    sup_entry_t *e = find_free();
    if (!e) return -1;

    e->proc = *proc;
    e->state = DANOS_SUP_STATE_STOPPED;
    e->restart_count = 0;
    e->current_delay_ms = proc->restart_min_ms;
    e->used = true;
    return 0;
}

int danos_sup_unregister(const char *name)
{
    sup_entry_t *e = find_proc(name);
    if (!e) return -1;
    e->used = false;
    return 0;
}

danos_sup_state_t danos_sup_get_state(const char *name)
{
    sup_entry_t *e = find_proc(name);
    if (!e) return DANOS_SUP_STATE_STOPPED;
    return e->state;
}

uint32_t danos_sup_get_restart_count(const char *name)
{
    sup_entry_t *e = find_proc(name);
    if (!e) return 0;
    return e->restart_count;
}

int danos_sup_start(void)
{
    if (!g_initialized) danos_sup_init();
    /* In production: fork+exec each process.
     * For now, mark as running (test mode). */
    for (int i = 0; i < SUP_MAX_PROCS; i++) {
        if (g_procs[i].used && g_procs[i].state == DANOS_SUP_STATE_STOPPED) {
            g_procs[i].state = DANOS_SUP_STATE_RUNNING;
        }
    }
    return 0;
}

int danos_sup_stop(void)
{
    for (int i = 0; i < SUP_MAX_PROCS; i++) {
        if (g_procs[i].used) {
            /* In production: send SIGTERM, wait, SIGKILL if needed */
            g_procs[i].state = DANOS_SUP_STATE_STOPPED;
        }
    }
    return 0;
}

/* Internal: called when a process exits.
 * Implements exponential backoff: delay *= 2, capped at restart_max_ms. */
void danos_sup_on_exit(const char *name)
{
    sup_entry_t *e = find_proc(name);
    if (!e) return;

    e->restart_count++;
    e->current_delay_ms *= 2;
    if (e->current_delay_ms > e->proc.restart_max_ms) {
        e->current_delay_ms = e->proc.restart_max_ms;
    }

    /* Check if exceeded max retries (simplified: 10 retries) */
    if (e->restart_count > 10) {
        e->state = e->proc.critical ? DANOS_SUP_STATE_SAFE_MODE
                                    : DANOS_SUP_STATE_FAILED;
    } else {
        e->state = DANOS_SUP_STATE_RESTARTING;
    }
}
