/*
 * DANOS-Open Core: Write-Ahead Log (WAL) Persistence (B6)
 *
 * Provides durable transaction logging:
 *   - Before commit, transaction operations are written to WAL file
 *   - On crash recovery, WAL is replayed to restore committed state
 *   - WAL is fsync'd before commit returns
 *   - Checkpoint truncates WAL after state is snapshot'd
 *
 * WAL record format:
 *   [magic:4][tx_id:8][op_type:1][obj_type:2][obj_id:4][data_len:4][data:N][crc:4]
 */

#ifndef DANOS_WAL_H__
#define DANOS_WAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WAL operation types */
typedef enum {
    WAL_OP_CREATE = 1,
    WAL_OP_UPDATE = 2,
    WAL_OP_DELETE = 3,
    WAL_OP_COMMIT = 4,   /* transaction commit marker */
    WAL_OP_ABORT  = 5,   /* transaction abort marker */
} wal_op_type_t;

/* WAL object types (matches DPA object types) */
typedef enum {
    WAL_OBJ_IFACE   = 1,
    WAL_OBJ_VRF     = 2,
    WAL_OBJ_NH      = 3,
    WAL_OBJ_NHGROUP = 4,
    WAL_OBJ_ROUTE   = 5,
    WAL_OBJ_ACL_TBL = 6,
    WAL_OBJ_ACL_RULE= 7,
    WAL_OBJ_QOS     = 8,
    WAL_OBJ_BFD     = 9,
} wal_obj_type_t;

/* WAL record */
typedef struct {
    uint32_t magic;        /* 0xDANOS01 */
    uint64_t tx_id;        /* transaction ID */
    uint8_t  op_type;      /* wal_op_type_t */
    uint16_t obj_type;     /* wal_obj_type_t */
    uint32_t obj_id;       /* object ID */
    uint32_t data_len;     /* payload length */
    const uint8_t *data;   /* payload (not owned) */
} wal_record_t;

#define WAL_MAGIC 0x444E4F53  /* "DNOS" */
#define WAL_HEADER_SIZE 23    /* 4+8+1+2+4+4 */

/* WAL context */
typedef struct {
    FILE *fp;
    char path[256];
    bool enabled;
    uint64_t records_written;
    uint64_t bytes_written;
} wal_ctx_t;

/* Initialize WAL context. path is the WAL file path. */
int danos_wal_init(wal_ctx_t *ctx, const char *path);

/* Close WAL file */
void danos_wal_close(wal_ctx_t *ctx);

/* Append a record to WAL. Returns 0 on success. */
int danos_wal_append(wal_ctx_t *ctx, const wal_record_t *rec);

/* Append a commit marker for a transaction */
int danos_wal_append_commit(wal_ctx_t *ctx, uint64_t tx_id);

/* Append an abort marker for a transaction */
int danos_wal_append_abort(wal_ctx_t *ctx, uint64_t tx_id);

/* Sync WAL to disk (fsync) */
int danos_wal_sync(wal_ctx_t *ctx);

/* Checkpoint: truncate WAL after successful state snapshot */
int danos_wal_checkpoint(wal_ctx_t *ctx);

/* Recovery: replay WAL records.
 * callback is called for each record. Returns number of records replayed. */
typedef int (*wal_replay_cb_t)(const wal_record_t *rec, void *user);
int danos_wal_replay(const char *path, wal_replay_cb_t callback, void *user);

/* Get statistics */
void danos_wal_get_stats(wal_ctx_t *ctx, uint64_t *records, uint64_t *bytes);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_WAL_H__ */
