/*
 * DANOS-Open Core: Backend programming pipeline (v0.9, ADR-0007)
 *
 * danos_programming_run() walks the desired-state store (the DPA
 * default store, where gNMI/CLI/NETCONF commits land) and drives the
 * active backend ops until every object is PROGRAMMED:
 *
 *   - Not programmed            -> issue create op, mark on success
 *   - Programmed, content drift -> issue update op, refresh marker
 *   - Marker without object     -> backend entry is stale; (delete
 *                                  tracking arrives with tombstones)
 *
 * The PROGRAMMED ledger is a private store keyed by (type, id) holding
 * a content hash, so re-runs are incremental and crash-safe: after a
 * restart the pipeline re-issues whatever is not yet programmed.
 */

#include <danos/core/backend_ops.h>
#include <danos/core/object_registry.h>
#include <danos/core/wal.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* object type -> ops field applicability */
static danos_status_t program_one(const danos_backend_ops_t *ops,
                                  danos_obj_type_t type, danos_obj_id_t id,
                                  const void *data, size_t size)
{
    if (type == DANOS_OBJ_IFACE && size >= sizeof(danos_iface_t)) {
        const danos_iface_t *i = data;
        return ops->iface_up ? ops->iface_up(i->ifindex, i->admin_up,
                                             ops->user)
                             : DANOS_ERR_NOT_SUPPORTED;
    }
    if (type == DANOS_OBJ_ROUTE && size >= sizeof(danos_route_t)) {
        const danos_route_t *r = data;
        if (r->nhgroup_id == 0 && !(r->flags & DANOS_ROUTE_FLAG_BLACKHOLE))
            return DANOS_ERR_INVALID_ARG;   /* no usable path yet */
        return ops->route_add ? ops->route_add(r, ops->user)
                              : DANOS_ERR_NOT_SUPPORTED;
    }
    if (type == DANOS_OBJ_VRF && size >= sizeof(danos_vrf_t)) {
        return ops->vrf_add ? ops->vrf_add(data, ops->user)
                            : DANOS_ERR_NOT_SUPPORTED;
    }
    return DANOS_ERR_NOT_SUPPORTED;
}

/* PROGRAMMED ledger: private store, id space keyed like the desired
 * store but with a distinct store instance; data = 8-byte content hash
 * of the desired blob. */
static danos_object_store_t *g_programmed;
static const danos_backend_ops_t *g_ops;

void danos_backend_ops_set(const danos_backend_ops_t *ops)
{
    g_ops = ops;
}

const danos_backend_ops_t *danos_backend_ops_get(void)
{
    return g_ops;
}

static uint64_t content_hash(const void *data, size_t size)
{
    uint64_t h = 14695981039346656037ULL;
    const uint8_t *b = data;
    for (size_t i = 0; i < size; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static danos_object_store_t *ledger(void)
{
    if (!g_programmed)
        g_programmed = danos_object_store_create(256);
    return g_programmed;
}

static void try_program(danos_object_entry_t *e, void *user)
{
    uint64_t *counters = user;   /* [0]=attempted [1]=ok [2]=failed */

    if (!g_ops) return;
    if (e->type == DANOS_OBJ_ACL || e->type == DANOS_OBJ_QOS ||
        e->type == DANOS_OBJ_QOS_BIND || e->type == DANOS_OBJ_BFD ||
        e->type == DANOS_OBJ_MPLS_LSP || e->type == DANOS_OBJ_TUNNEL ||
        e->type == DANOS_OBJ_EVPN || e->type == DANOS_OBJ_MULTICAST ||
        e->type == DANOS_OBJ_NEXTHOP || e->type == DANOS_OBJ_NHGROUP)
        return;   /* not yet programmable by this pipeline (v0.9 scope) */

    uint64_t hash = content_hash(e->data, e->data_size);

    /* already programmed and unchanged? */
    uint8_t cur[8];
    size_t cur_size = sizeof(cur);
    if (danos_object_read(ledger(), e->type, e->id, cur, &cur_size)
            == DANOS_OK && cur_size == 8) {
        uint64_t prev;
        memcpy(&prev, cur, 8);
        if (prev == hash) return;   /* in sync */
        /* drift: update through the same create path (idempotent ops) */
    }

    counters[0]++;
    danos_status_t st = program_one(g_ops, e->type, e->id,
                                    e->data, e->data_size);
    if (st != DANOS_OK) {
        counters[2]++;
        return;
    }
    counters[1]++;
    uint64_t h = hash;
    if (danos_object_read(ledger(), e->type, e->id, cur, &cur_size)
            == DANOS_OK)
        (void)danos_object_update(ledger(), e->type, e->id, &h, 8);
    else
        (void)danos_object_create(ledger(), e->type, e->id, &h, 8);
}

uint64_t danos_programming_run(uint64_t *attempted, uint64_t *failed)
{
    uint64_t counters[3] = {0, 0, 0};
    if (!g_ops || !g_default_store) {
        if (attempted) *attempted = 0;
        if (failed) *failed = 0;
        return 0;
    }
    danos_object_iterate(g_default_store, try_program, counters);
    if (attempted) *attempted = counters[0];
    if (failed)    *failed    = counters[2];
    return counters[1];
}

uint64_t danos_programming_programmed_count(danos_obj_type_t type)
{
    return danos_object_count(ledger(), type);
}
