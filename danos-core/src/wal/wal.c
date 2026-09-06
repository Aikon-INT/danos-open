/*
 * DANOS-Open Core: WAL Implementation (B6)
 *
 * Write-Ahead Log for transaction persistence.
 */

#include <danos/core/wal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Simple CRC32 */
static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

int danos_wal_init(wal_ctx_t *ctx, const char *path)
{
    memset(ctx, 0, sizeof(*ctx));
    if (!path) return -1;

    snprintf(ctx->path, sizeof(ctx->path), "%s", path);

    /* Open for append (create if not exists) */
    ctx->fp = fopen(path, "ab+");
    if (!ctx->fp) return -1;

    ctx->enabled = true;
    return 0;
}

void danos_wal_close(wal_ctx_t *ctx)
{
    if (ctx->fp) {
        fflush(ctx->fp);
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
    ctx->enabled = false;
}

int danos_wal_append(wal_ctx_t *ctx, const wal_record_t *rec)
{
    if (!ctx || !ctx->enabled || !ctx->fp || !rec) return -1;

    /* Write header */
    uint8_t header[WAL_HEADER_SIZE];
    uint32_t magic = WAL_MAGIC;
    memcpy(header, &magic, 4);
    memcpy(header + 4, &rec->tx_id, 8);
    header[12] = rec->op_type;
    memcpy(header + 13, &rec->obj_type, 2);
    memcpy(header + 15, &rec->obj_id, 4);
    memcpy(header + 19, &rec->data_len, 4);

    /* CRC over header + data */
    uint32_t crc = crc32_calc(header, WAL_HEADER_SIZE);
    if (rec->data_len > 0 && rec->data) {
        crc = crc32_calc(rec->data, rec->data_len) ^ crc;
    }

    size_t written = 0;
    written += fwrite(header, 1, WAL_HEADER_SIZE, ctx->fp);
    if (rec->data_len > 0 && rec->data) {
        written += fwrite(rec->data, 1, rec->data_len, ctx->fp);
    }
    written += fwrite(&crc, 1, 4, ctx->fp);

    if (written != WAL_HEADER_SIZE + rec->data_len + 4) return -1;

    ctx->records_written++;
    ctx->bytes_written += written;
    return 0;
}

int danos_wal_append_commit(wal_ctx_t *ctx, uint64_t tx_id)
{
    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WAL_MAGIC;
    rec.tx_id = tx_id;
    rec.op_type = WAL_OP_COMMIT;
    rec.data_len = 0;
    return danos_wal_append(ctx, &rec);
}

int danos_wal_append_abort(wal_ctx_t *ctx, uint64_t tx_id)
{
    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WAL_MAGIC;
    rec.tx_id = tx_id;
    rec.op_type = WAL_OP_ABORT;
    rec.data_len = 0;
    return danos_wal_append(ctx, &rec);
}

int danos_wal_sync(wal_ctx_t *ctx)
{
    if (!ctx || !ctx->fp) return -1;
    fflush(ctx->fp);
    int fd = fileno(ctx->fp);
    if (fd < 0) return -1;
    return fsync(fd);
}

int danos_wal_checkpoint(wal_ctx_t *ctx)
{
    if (!ctx || !ctx->fp) return -1;

    /* Close and truncate */
    fflush(ctx->fp);
    fclose(ctx->fp);
    ctx->fp = NULL;

    /* Truncate WAL file */
    int fd = open(ctx->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);

    /* Reopen for append */
    ctx->fp = fopen(ctx->path, "ab+");
    if (!ctx->fp) return -1;

    return 0;
}

int danos_wal_replay(const char *path, wal_replay_cb_t callback, void *user)
{
    if (!path || !callback) return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;  /* no WAL file = nothing to replay */

    int replayed = 0;
    uint8_t header[WAL_HEADER_SIZE];

    while (fread(header, 1, WAL_HEADER_SIZE, fp) == WAL_HEADER_SIZE) {
        /* Parse header */
        uint32_t magic;
        memcpy(&magic, header, 4);
        if (magic != WAL_MAGIC) break;

        wal_record_t rec;
        rec.magic = magic;
        memcpy(&rec.tx_id, header + 4, 8);
        rec.op_type = header[12];
        memcpy(&rec.obj_type, header + 13, 2);
        memcpy(&rec.obj_id, header + 15, 4);
        memcpy(&rec.data_len, header + 19, 4);

        /* Sanity check data_len */
        if (rec.data_len > 1024 * 1024) break;  /* 1MB max */

        /* Read data */
        uint8_t *data = NULL;
        if (rec.data_len > 0) {
            data = malloc(rec.data_len);
            if (!data) break;
            if (fread(data, 1, rec.data_len, fp) != rec.data_len) {
                free(data);
                break;
            }
            rec.data = data;
        } else {
            rec.data = NULL;
        }

        /* Read and verify CRC */
        uint32_t stored_crc, calc_crc;
        if (fread(&stored_crc, 1, 4, fp) != 4) {
            free(data);
            break;
        }
        calc_crc = crc32_calc(header, WAL_HEADER_SIZE);
        if (rec.data_len > 0 && data) {
            calc_crc = crc32_calc(data, rec.data_len) ^ calc_crc;
        }

        if (calc_crc != stored_crc) {
            /* CRC mismatch — stop replay */
            free(data);
            break;
        }

        /* Invoke callback */
        callback(&rec, user);
        replayed++;
        free(data);
    }

    fclose(fp);
    return replayed;
}

void danos_wal_get_stats(wal_ctx_t *ctx, uint64_t *records, uint64_t *bytes)
{
    if (records) *records = ctx->records_written;
    if (bytes)   *bytes   = ctx->bytes_written;
}
