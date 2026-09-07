/*
 * DANOS-Open Security: Audit Log Interface
 *
 * Append-only audit log for all DPA Transactions.
 * Design: v1.1/security/security_architecture.md §20.9
 */

#ifndef DANOS_SECURITY_AUDIT_H__
#define DANOS_SECURITY_AUDIT_H__

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audit event types */
typedef enum {
    DANOS_AUDIT_TX_BEGIN    = 1,
    DANOS_AUDIT_TX_COMMIT   = 2,
    DANOS_AUDIT_TX_ABORT    = 3,
    DANOS_AUDIT_TX_ROLLBACK = 4,
    DANOS_AUDIT_AUTH_OK     = 5,
    DANOS_AUDIT_AUTH_FAIL   = 6,
    DANOS_AUDIT_PERMIT      = 7,
    DANOS_AUDIT_DENY        = 8,
    DANOS_AUDIT_KEY_ROTATE  = 9,
} danos_audit_event_t;

/* Audit log entry.
 * All fields required for traceability (§20.9). */
typedef struct {
    uint64_t            tx_id;       /* Transaction ID */
    danos_audit_event_t event;       /* event type */
    char                initiator[64]; /* user/service identity */
    char                source_ip[46];  /* source IP (IPv6 max 45+1) */
    char                obj_type[32];   /* object type name */
    char                obj_id[64];     /* object identifier */
    char                diff[256];      /* config diff (truncated) */
    struct timespec     timestamp;      /* event time */
} danos_audit_entry_t;

/* Append an audit entry to the log.
 * The log is append-only (§20.9.2).
 * Returns 0 on success, negative on error. */
int danos_audit_log(const danos_audit_entry_t *entry);

/* Query audit log (simple filter by tx_id).
 * Fills entries array, returns count. */
int danos_audit_query(uint64_t tx_id,
                      danos_audit_entry_t *entries, int max_entries);

/* Initialize audit log.
 * path: log file path (NULL = default /var/log/danos/audit.log)
 * Returns 0 on success. */
int danos_audit_init(const char *path);

/* Close audit log. */
void danos_audit_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_SECURITY_AUDIT_H__ */
