/*
 * DANOS-Open Security: Audit Log Implementation
 *
 * Append-only audit log per §20.9.
 * Format: JSON lines (one entry per line) for easy parsing.
 */

#include <danos/security/audit.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define DEFAULT_AUDIT_PATH "/var/log/danos/audit.log"
#define MAX_ENTRIES 4096

static FILE *g_log_fp = NULL;
static char g_path[256] = DEFAULT_AUDIT_PATH;

/* In-memory ring buffer for query */
static danos_audit_entry_t g_ring[MAX_ENTRIES];
static int g_ring_head = 0;
static int g_ring_count = 0;

static const char *event_name(danos_audit_event_t e)
{
    switch (e) {
    case DANOS_AUDIT_TX_BEGIN:    return "tx_begin";
    case DANOS_AUDIT_TX_COMMIT:   return "tx_commit";
    case DANOS_AUDIT_TX_ABORT:    return "tx_abort";
    case DANOS_AUDIT_TX_ROLLBACK: return "tx_rollback";
    case DANOS_AUDIT_AUTH_OK:     return "auth_ok";
    case DANOS_AUDIT_AUTH_FAIL:   return "auth_fail";
    case DANOS_AUDIT_PERMIT:      return "permit";
    case DANOS_AUDIT_DENY:        return "deny";
    case DANOS_AUDIT_KEY_ROTATE:  return "key_rotate";
    default:                      return "unknown";
    }
}

int danos_audit_init(const char *path)
{
    if (path) {
        snprintf(g_path, sizeof(g_path), "%s", path);
    }

    /* Try to open log file; if fails, use in-memory only */
    g_log_fp = fopen(g_path, "a");
    /* If open fails (e.g. no /var/log), continue with in-memory only */

    g_ring_head = 0;
    g_ring_count = 0;
    return 0;
}

void danos_audit_close(void)
{
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

int danos_audit_log(const danos_audit_entry_t *entry)
{
    if (!entry) return -1;
    if (!g_log_fp && g_ring_count == 0) {
        /* Lazy init if not initialized */
        danos_audit_init(NULL);
    }

    /* Write to in-memory ring buffer */
    g_ring[g_ring_head] = *entry;
    g_ring_head = (g_ring_head + 1) % MAX_ENTRIES;
    if (g_ring_count < MAX_ENTRIES) g_ring_count++;

    /* Write to file (append-only, JSON line) */
    if (g_log_fp) {
        fprintf(g_log_fp,
            "{\"ts\":%ld.%09ld,\"tx_id\":%lu,\"event\":\"%s\","
            "\"initiator\":\"%s\",\"src\":\"%s\","
            "\"obj_type\":\"%s\",\"obj_id\":\"%s\",\"diff\":\"%s\"}\n",
            (long)entry->timestamp.tv_sec, (long)entry->timestamp.tv_nsec,
            (unsigned long)entry->tx_id,
            event_name(entry->event),
            entry->initiator, entry->source_ip,
            entry->obj_type, entry->obj_id, entry->diff);
        fflush(g_log_fp);
    }

    return 0;
}

int danos_audit_query(uint64_t tx_id,
                      danos_audit_entry_t *entries, int max_entries)
{
    if (!entries || max_entries <= 0) return 0;

    int count = 0;
    int start = (g_ring_count < MAX_ENTRIES) ? 0 : g_ring_head;

    for (int i = 0; i < g_ring_count && count < max_entries; i++) {
        int idx = (start + i) % MAX_ENTRIES;
        if (tx_id == 0 || g_ring[idx].tx_id == tx_id) {
            entries[count++] = g_ring[idx];
        }
    }
    return count;
}
