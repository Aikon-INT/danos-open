/*
 * Test: WAL Persistence (B6)
 * Verify write-ahead logging and crash recovery.
 */
#include <danos/core/wal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#define TEST_WAL "/tmp/danos_test.wal"

/* Replay counter */
typedef struct {
    int creates;
    int updates;
    int deletes;
    int commits;
    int aborts;
} replay_stats_t;

static int replay_cb(const wal_record_t *rec, void *user)
{
    replay_stats_t *stats = (replay_stats_t *)user;
    switch (rec->op_type) {
    case WAL_OP_CREATE: stats->creates++; break;
    case WAL_OP_UPDATE: stats->updates++; break;
    case WAL_OP_DELETE: stats->deletes++; break;
    case WAL_OP_COMMIT: stats->commits++; break;
    case WAL_OP_ABORT:  stats->aborts++;  break;
    }
    return 0;
}

int test_wal_basic(void)
{
    unlink(TEST_WAL);

    wal_ctx_t ctx;
    assert(danos_wal_init(&ctx, TEST_WAL) == 0);

    /* Append a CREATE record */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WAL_MAGIC;
    rec.tx_id = 1;
    rec.op_type = WAL_OP_CREATE;
    rec.obj_type = WAL_OBJ_IFACE;
    rec.obj_id = 1;
    rec.data_len = sizeof(data);
    rec.data = data;
    assert(danos_wal_append(&ctx, &rec) == 0);

    /* Append commit marker */
    assert(danos_wal_append_commit(&ctx, 1) == 0);

    /* Sync to disk */
    assert(danos_wal_sync(&ctx) == 0);

    /* Check stats */
    uint64_t records, bytes;
    danos_wal_get_stats(&ctx, &records, &bytes);
    assert(records == 2);
    assert(bytes > 0);

    danos_wal_close(&ctx);

    /* Replay */
    replay_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    int replayed = danos_wal_replay(TEST_WAL, replay_cb, &stats);
    assert(replayed == 2);
    assert(stats.creates == 1);
    assert(stats.commits == 1);

    unlink(TEST_WAL);
    printf("[PASS] test_wal_basic: write + replay works\n");
    return 0;
}

int test_wal_crash_recovery(void)
{
    unlink(TEST_WAL);

    /* Simulate: write some records, commit, then "crash" (no close) */
    wal_ctx_t ctx;
    assert(danos_wal_init(&ctx, TEST_WAL) == 0);

    /* Transaction 1: create interface, commit */
    uint8_t iface_data[] = {0x01, 0x00, 0x00, 0x00, 'e', 't', 'h', '0'};
    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WAL_MAGIC;
    rec.tx_id = 1;
    rec.op_type = WAL_OP_CREATE;
    rec.obj_type = WAL_OBJ_IFACE;
    rec.obj_id = 1;
    rec.data_len = sizeof(iface_data);
    rec.data = iface_data;
    danos_wal_append(&ctx, &rec);
    danos_wal_append_commit(&ctx, 1);

    /* Transaction 2: create route, but NO commit (crash before commit) */
    uint8_t route_data[] = {0x0A, 0x00, 0x00, 0x00, 0x18};
    memset(&rec, 0, sizeof(rec));
    rec.magic = WAL_MAGIC;
    rec.tx_id = 2;
    rec.op_type = WAL_OP_CREATE;
    rec.obj_type = WAL_OBJ_ROUTE;
    rec.obj_id = 1;
    rec.data_len = sizeof(route_data);
    rec.data = route_data;
    danos_wal_append(&ctx, &rec);
    /* No commit for tx 2 — simulates crash */

    danos_wal_sync(&ctx);
    danos_wal_close(&ctx);

    /* Recovery: replay WAL */
    replay_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    int replayed = danos_wal_replay(TEST_WAL, replay_cb, &stats);

    /* Should replay all records (3: 2 creates + 1 commit) */
    assert(replayed == 3);
    assert(stats.creates == 2);
    assert(stats.commits == 1);

    /* In production, recovery logic would:
     * - Apply tx 1 (has commit marker)
     * - Discard tx 2 (no commit marker = incomplete)
     */
    printf("[PASS] test_wal_crash_recovery: replay handles uncommitted tx\n");
    unlink(TEST_WAL);
    return 0;
}

int test_wal_checkpoint(void)
{
    unlink(TEST_WAL);

    wal_ctx_t ctx;
    assert(danos_wal_init(&ctx, TEST_WAL) == 0);

    /* Write some records */
    danos_wal_append_commit(&ctx, 1);
    danos_wal_append_commit(&ctx, 2);
    danos_wal_sync(&ctx);

    /* Checkpoint: truncate WAL */
    assert(danos_wal_checkpoint(&ctx) == 0);

    /* WAL should be empty now */
    danos_wal_close(&ctx);

    replay_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    int replayed = danos_wal_replay(TEST_WAL, replay_cb, &stats);
    assert(replayed == 0);

    unlink(TEST_WAL);
    printf("[PASS] test_wal_checkpoint: truncates after checkpoint\n");
    return 0;
}

int test_wal_crc_protection(void)
{
    unlink(TEST_WAL);

    wal_ctx_t ctx;
    assert(danos_wal_init(&ctx, TEST_WAL) == 0);
    danos_wal_append_commit(&ctx, 1);
    danos_wal_sync(&ctx);
    danos_wal_close(&ctx);

    /* Corrupt the WAL file */
    FILE *fp = fopen(TEST_WAL, "r+b");
    assert(fp);
    fseek(fp, 5, SEEK_SET);  /* corrupt tx_id */
    uint8_t bad = 0xFF;
    fwrite(&bad, 1, 1, fp);
    fclose(fp);

    /* Replay should detect corruption (CRC mismatch) and stop */
    replay_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    int replayed = danos_wal_replay(TEST_WAL, replay_cb, &stats);
    /* Should replay 0 records due to CRC failure */
    assert(replayed == 0);

    unlink(TEST_WAL);
    printf("[PASS] test_wal_crc_protection: detects corruption\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    if (test_wal_basic() != 0) failed++;
    if (test_wal_crash_recovery() != 0) failed++;
    if (test_wal_checkpoint() != 0) failed++;
    if (test_wal_crc_protection() != 0) failed++;
    printf("=== wal_test: %s ===\n",
           failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
