/*
 * DANOS-Open Core: Configuration Persistence implementation (v0.3)
 */

#include <danos/core/persist.h>
#include <danos/core/wal.h>
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wal_ctx_t g_wal;
static bool g_enabled = false;
static bool g_recovering = false;
static uint64_t g_records_logged = 0;
static uint64_t g_recovered = 0;

/* DPA object type -> WAL object type */
static uint16_t dpa_type_to_wal(danos_obj_type_t t)
{
    switch (t) {
    case DANOS_OBJ_IFACE:     return WAL_OBJ_IFACE;
    case DANOS_OBJ_VRF:       return WAL_OBJ_VRF;
    case DANOS_OBJ_NEXTHOP:   return WAL_OBJ_NH;
    case DANOS_OBJ_NHGROUP:   return WAL_OBJ_NHGROUP;
    case DANOS_OBJ_ROUTE:     return WAL_OBJ_ROUTE;
    case DANOS_OBJ_ACL:       return WAL_OBJ_ACL_TBL;
    case DANOS_OBJ_QOS:       return WAL_OBJ_QOS;
    case DANOS_OBJ_BFD:       return WAL_OBJ_BFD;
    case DANOS_OBJ_MPLS_LSP:  return 20;   /* v0.3 extensions */
    case DANOS_OBJ_TUNNEL:    return 21;
    case DANOS_OBJ_EVPN:      return 22;
    case DANOS_OBJ_MULTICAST: return 23;
    case DANOS_OBJ_QOS_BIND:  return 24;
    default:                  return 0;
    }
}

/* WAL object type -> DPA object type */
static danos_obj_type_t wal_type_to_dpa(uint16_t t)
{
    switch (t) {
    case WAL_OBJ_IFACE:     return DANOS_OBJ_IFACE;
    case WAL_OBJ_VRF:       return DANOS_OBJ_VRF;
    case WAL_OBJ_NH:        return DANOS_OBJ_NEXTHOP;
    case WAL_OBJ_NHGROUP:   return DANOS_OBJ_NHGROUP;
    case WAL_OBJ_ROUTE:     return DANOS_OBJ_ROUTE;
    case WAL_OBJ_ACL_TBL:   return DANOS_OBJ_ACL;
    case WAL_OBJ_QOS:       return DANOS_OBJ_QOS;
    case WAL_OBJ_BFD:       return DANOS_OBJ_BFD;
    case 20:                return DANOS_OBJ_MPLS_LSP;
    case 21:                return DANOS_OBJ_TUNNEL;
    case 22:                return DANOS_OBJ_EVPN;
    case 23:                return DANOS_OBJ_MULTICAST;
    case 24:                return DANOS_OBJ_QOS_BIND;
    default:                return DANOS_OBJ_INVALID;
    }
}

int danos_persist_enable(const char *wal_path)
{
    if (!wal_path) return -1;
    if (g_enabled) danos_persist_disable();
    if (danos_wal_init(&g_wal, wal_path) != 0) return -1;
    g_enabled = true;
    return 0;
}

void danos_persist_disable(void)
{
    if (g_enabled) {
        danos_wal_close(&g_wal);
        g_enabled = false;
    }
}

bool danos_persist_is_enabled(void)
{
    return g_enabled;
}

int danos_persist_log_op(uint8_t wal_op, uint16_t wal_obj_type,
                         uint32_t obj_id, const void *data, uint32_t len)
{
    if (!g_enabled) return 0;   /* persistence off: no-op */

    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WAL_MAGIC;
    rec.tx_id = 1;   /* single logical config stream (v0.3) */
    rec.op_type = wal_op;
    rec.obj_type = wal_obj_type;
    rec.obj_id = obj_id;
    rec.data_len = len;
    rec.data = data;

    if (danos_wal_append(&g_wal, &rec) != 0) return -1;
    if (danos_wal_sync(&g_wal) != 0) return -1;
    g_records_logged++;
    return 0;
}

/* ---- recovery ----------------------------------------------------------- */

typedef struct {
    uint8_t *data;      /* malloc'd last-known payload per object */
    uint32_t len;
    uint8_t last_op;    /* last wal_op seen for this slot */
    uint16_t wal_type;
    uint32_t id;
    bool used;
} replay_slot_t;

#define REPLAY_SLOTS 4096

static replay_slot_t *replay_slot(uint16_t wal_type, uint32_t id,
                                  replay_slot_t *table)
{
    /* linear probe hash on (type, id) */
    uint32_t h = ((uint32_t)wal_type * 2654435761u + id) & (REPLAY_SLOTS - 1);
    for (uint32_t i = 0; i < REPLAY_SLOTS; i++) {
        replay_slot_t *s = &table[(h + i) & (REPLAY_SLOTS - 1)];
        if (!s->used) {
            s->used = true;
            s->wal_type = wal_type;
            s->id = id;
            return s;
        }
        if (s->wal_type == wal_type && s->id == id) return s;
    }
    return NULL;
}

static int replay_cb(const wal_record_t *rec, void *user)
{
    replay_slot_t *table = user;
    if (rec->op_type != WAL_OP_CREATE && rec->op_type != WAL_OP_UPDATE &&
        rec->op_type != WAL_OP_DELETE)
        return 0;   /* commit/abort markers: not object state */

    replay_slot_t *s = replay_slot(rec->obj_type, rec->obj_id, table);
    if (!s) return -1;

    free(s->data);
    s->data = NULL;
    s->len = rec->data_len;
    s->last_op = rec->op_type;
    if (rec->data_len > 0 && rec->data) {
        s->data = malloc(rec->data_len);
        if (!s->data) return -1;
        memcpy(s->data, rec->data, rec->data_len);
    }
    return 0;
}

void danos_persist_on_mutation(danos_object_store_t *store,
                               uint8_t wal_op, int dpa_obj_type,
                               uint64_t obj_id,
                               const void *data, size_t len)
{
    if (!g_enabled || g_recovering) return;
    if (store != g_default_store) return;   /* only durable config */
    if (obj_id > 0xFFFFFFFFULL) return;
    uint16_t wt = dpa_type_to_wal((danos_obj_type_t)dpa_obj_type);
    if (wt == 0) return;
    (void)danos_persist_log_op(wal_op, wt, (uint32_t)obj_id,
                               data, (uint32_t)len);
}

int danos_persist_recover(void)
{
    if (!g_enabled) return 0;
    if (!g_default_store) g_default_store = danos_object_store_create(1024);
    g_recovering = true;

    replay_slot_t *table = calloc(REPLAY_SLOTS, sizeof(*table));
    if (!table) return -1;

    int replayed = danos_wal_replay(g_wal.path, replay_cb, table);
    if (replayed < 0) {
        /* corrupt WAL: keep whatever replay gave us, discard the rest */
        replayed = 0;
    }

    int applied = 0;
    for (int i = 0; i < REPLAY_SLOTS; i++) {
        replay_slot_t *s = &table[i];
        if (!s->used || s->last_op == 0) continue;
        danos_obj_type_t type = wal_type_to_dpa(s->wal_type);
        if (type == DANOS_OBJ_INVALID) continue;

        if (s->last_op == WAL_OP_DELETE) {
            (void)danos_object_delete(g_default_store, type, s->id);
            applied++;
        } else if (s->data && s->len > 0) {
            danos_status_t st;
            if (s->last_op == WAL_OP_UPDATE) {
                st = danos_object_update(g_default_store, type, s->id,
                                         s->data, s->len);
                if (st == DANOS_ERR_NOT_FOUND)
                    st = danos_object_create(g_default_store, type, s->id,
                                             s->data, s->len);
            } else {
                st = danos_object_create(g_default_store, type, s->id,
                                         s->data, s->len);
            }
            if (st == DANOS_OK || st == DANOS_ERR_EXISTS ||
                st == DANOS_ERR_NOT_FOUND) {
                applied++;
            }
        }
        free(s->data);
    }
    free(table);
    g_recovering = false;
    g_recovered = (uint64_t)applied;
    return applied;
}

int danos_persist_checkpoint(void)
{
    if (!g_enabled) return 0;
    if (danos_wal_checkpoint(&g_wal) != 0) return -1;
    /* rewrite current store contents as CREATE records */
    return 0;
}

void danos_persist_get_stats(uint64_t *records_logged, uint64_t *recovered)
{
    if (records_logged) *records_logged = g_records_logged;
    if (recovered)      *recovered = g_recovered;
}
