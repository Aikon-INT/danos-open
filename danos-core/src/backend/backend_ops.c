/*
 * DANOS-Open Core: Backend programming pipeline (v0.9/v0.10, ADR-0007)
 *
 * danos_programming_run() walks the desired-state store (the DPA
 * default store, where gNMI/CLI/NETCONF commits land) and drives the
 * active backend ops until every object is PROGRAMMED.
 *
 * The PROGRAMMED ledger is a private store keyed by (type, id) holding
 * a full copy of the last-programmed object bytes:
 *   - desired == ledger  -> in sync, nothing to do
 *   - desired != ledger  -> re-issue (ops are idempotent)
 *   - desired missing    -> tombstone: withdraw from backend (routes)
 *                           and drop the ledger entry
 * Crash-safe: the ledger is rebuilt from scratch, so a restart re-
 * issues whatever is not yet programmed.
 */

#include <danos/core/backend_ops.h>
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const danos_backend_ops_t *g_ops;
static danos_object_store_t *g_programmed;

void danos_backend_ops_set(const danos_backend_ops_t *ops)
{
    g_ops = ops;
}

const danos_backend_ops_t *danos_backend_ops_get(void)
{
    return g_ops;
}

static danos_object_store_t *ledger(void)
{
    if (!g_programmed)
        g_programmed = danos_object_store_create(256);
    return g_programmed;
}

/* ---- dependency digest (v0.11) -------------------------------------------
 * A route's programmed state depends on the NH group and the NH object
 * it resolves through. The ledger records their digest alongside the
 * route bytes, so a gateway change (or NH deletion) invalidates the
 * entry even though the route object itself is unchanged. */

static uint64_t hash_bytes(const void *data, size_t size)
{
    uint64_t h = 14695981039346656037ULL;
    const uint8_t *b = data;
    for (size_t i = 0; i < size; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t dep_digest(danos_obj_type_t type, const void *data, size_t size)
{
    uint64_t h = 0;
    if (type != DANOS_OBJ_ROUTE || size < sizeof(danos_route_t)) return h;
    const danos_route_t *r = data;
    if (r->nhgroup_id == 0) return h;

    danos_nhgroup_t grp;
    size_t sz = sizeof(grp);
    if (danos_object_read(g_default_store, DANOS_OBJ_NHGROUP, r->nhgroup_id,
                          &grp, &sz) != DANOS_OK) {
        return 0xDEADBEEFULL;   /* group gone: digest differs -> dirty */
    }
    h = hash_bytes(&grp, sizeof(grp));
    for (uint32_t i = 0; i < grp.nh_count && i < 64; i++) {
        danos_nexthop_t nh;
        sz = sizeof(nh);
        if (danos_object_read(g_default_store, DANOS_OBJ_NEXTHOP,
                              grp.nh_ids[i], &nh, &sz) == DANOS_OK) {
            h ^= hash_bytes(&nh, sizeof(nh));
        } else {
            h ^= 0x535A4AULL;  /* member gone */
        }
    }
    return h;
}

/* ---- route next-hop resolution ------------------------------------------ */

/* nhgroup -> first member NH object -> gateway + egress ifindex */
static danos_status_t resolve_route_nh(const danos_route_t *r,
                                       danos_resolved_route_t *out)
{
    memset(out, 0, sizeof(*out));
    out->route = *r;

    if (r->flags & DANOS_ROUTE_FLAG_BLACKHOLE) return DANOS_OK;  /* no gw */
    if (r->nhgroup_id == 0) return DANOS_ERR_RETRY;   /* path not usable yet */

    danos_nhgroup_t grp;
    size_t sz = sizeof(grp);
    if (danos_object_read(g_default_store, DANOS_OBJ_NHGROUP, r->nhgroup_id,
                          &grp, &sz) != DANOS_OK || grp.nh_count == 0)
        return DANOS_ERR_RETRY;

    danos_nexthop_t nh;
    sz = sizeof(nh);
    if (danos_object_read(g_default_store, DANOS_OBJ_NEXTHOP,
                          grp.nh_ids[0], &nh, &sz) != DANOS_OK)
        return DANOS_ERR_RETRY;

    if (nh.gateway.af == DANOS_AF_IPV4) {
        out->has_gw = true;
        memcpy(out->gw, nh.gateway.addr, 4);
    } else if (nh.gateway.af == DANOS_AF_IPV6) {
        out->has_gw = true;
        memcpy(out->gw, nh.gateway.addr, 16);
    }
    out->oif = nh.ifindex;
    return DANOS_OK;
}

static danos_status_t program_one(danos_obj_type_t type, danos_obj_id_t id,
                                  const void *data, size_t size)
{
    (void)id;
    if (type == DANOS_OBJ_IFACE && size >= sizeof(danos_iface_t)) {
        const danos_iface_t *i = data;
        return g_ops->iface_up ? g_ops->iface_up(i->ifindex, i->admin_up,
                                                 g_ops->user)
                               : DANOS_ERR_NOT_SUPPORTED;
    }
    if (type == DANOS_OBJ_ROUTE && size >= sizeof(danos_route_t)) {
        const danos_route_t *r = data;
        if (r->nhgroup_id == 0 && !(r->flags & DANOS_ROUTE_FLAG_BLACKHOLE))
            return DANOS_ERR_RETRY;   /* path not usable yet */
        danos_resolved_route_t rr;
        danos_status_t st = resolve_route_nh(r, &rr);
        if (st != DANOS_OK) return st;
        return g_ops->route_add ? g_ops->route_add(&rr, g_ops->user)
                                : DANOS_ERR_NOT_SUPPORTED;
    }
    if (type == DANOS_OBJ_VRF && size >= sizeof(danos_vrf_t)) {
        return g_ops->vrf_add ? g_ops->vrf_add(data, g_ops->user)
                              : DANOS_ERR_NOT_SUPPORTED;
    }
    return DANOS_ERR_NOT_SUPPORTED;
}

/* types the v0.9/v0.10 pipeline cannot program */
static bool type_skipped(danos_obj_type_t t)
{
    return t == DANOS_OBJ_ACL || t == DANOS_OBJ_QOS ||
           t == DANOS_OBJ_QOS_BIND || t == DANOS_OBJ_BFD ||
           t == DANOS_OBJ_MPLS_LSP || t == DANOS_OBJ_TUNNEL ||
           t == DANOS_OBJ_EVPN || t == DANOS_OBJ_MULTICAST ||
           t == DANOS_OBJ_NEXTHOP || t == DANOS_OBJ_NHGROUP;
}

/* ---- desired-state pass -------------------------------------------------- */

typedef struct {
    uint64_t attempted, ok, failed;
} program_ctx_t;

/* ledger record layout: [u64 dep_digest][object bytes] */
static void program_entry(danos_object_entry_t *e, void *user)
{
    program_ctx_t *c = user;
    if (type_skipped(e->type)) return;

    uint64_t dep = dep_digest(e->type, e->data, e->data_size);

    /* in sync with ledger (object bytes AND dependency digest)? */
    uint8_t last[512];
    size_t lsz = sizeof(last);
    bool have_last = danos_object_read(ledger(), e->type, e->id,
                                       last, &lsz) == DANOS_OK;
    if (have_last && lsz == e->data_size + 8) {
        uint64_t last_dep;
        memcpy(&last_dep, last, 8);
        if (last_dep == dep &&
            memcmp(last + 8, e->data, e->data_size) == 0)
            return;   /* in sync */
    }

    c->attempted++;
    danos_status_t st = program_one(e->type, e->id, e->data, e->data_size);
    if (st != DANOS_OK) {
        c->failed++;
        /* v0.11: a previously-programmed route whose next hop became
         * unusable must be WITHDRAWN, not left forwarding via a stale
         * gateway. */
        if (st == DANOS_ERR_RETRY && have_last &&
            e->type == DANOS_OBJ_ROUTE && g_ops->route_del &&
            lsz >= 8 + sizeof(danos_route_t)) {
            danos_resolved_route_t rr;
            memset(&rr, 0, sizeof(rr));
            memcpy(&rr.route, last + 8, sizeof(danos_route_t));
            if (g_ops->route_del(&rr, g_ops->user) == DANOS_OK) {
                danos_object_delete(ledger(), e->type, e->id);
                c->failed--;
            }
        }
        return;
    }
    c->ok++;

    uint8_t rec[520];
    memcpy(rec, &dep, 8);
    memcpy(rec + 8, e->data, e->data_size);
    if (have_last && lsz == e->data_size + 8)
        (void)danos_object_update(ledger(), e->type, e->id, rec,
                                  e->data_size + 8);
    else
        (void)danos_object_create(ledger(), e->type, e->id, rec,
                                  e->data_size + 8);
}

uint64_t danos_programming_run(uint64_t *attempted, uint64_t *failed)
{
    program_ctx_t c = {0, 0, 0};
    if (g_ops && g_default_store)
        danos_object_iterate(g_default_store, program_entry, &c);
    if (attempted) *attempted = c.attempted;
    if (failed)    *failed    = c.failed;
    return c.ok;
}

uint64_t danos_programming_programmed_count(danos_obj_type_t type)
{
    return danos_object_count(ledger(), type);
}

/* ---- v0.10: tombstone sweep ----------------------------------------------
 * Ledger entries whose desired object disappeared: routes are withdrawn
 * through ops->route_del (the ledger holds the original bytes, so the
 * withdraw message is fully reconstructible); other types drop their
 * ledger entry. Failures keep the entry for the next sweep.
 */

typedef struct {
    uint64_t issued, failed;
} sweep_ctx_t;

/* The sweep must not mutate the ledger while iterating it (the iterate
 * holds a read lock), so candidates are collected first and withdrawn
 * after the walk completes. */
#define SWEEP_MAX 256

typedef struct {
    danos_obj_type_t type;
    danos_obj_id_t   id;
} sweep_id_t;

static sweep_id_t g_sweep_ids[SWEEP_MAX];
static unsigned g_sweep_n;

static void sweep_collect(danos_object_entry_t *e, void *user)
{
    (void)user;
    if (g_sweep_n >= SWEEP_MAX) return;

    uint8_t last[512];
    size_t lsz = sizeof(last);
    if (danos_object_read(ledger(), e->type, e->id, last, &lsz) != DANOS_OK)
        return;   /* not ours */

    /* still desired? */
    uint8_t probe[512];
    size_t psz = sizeof(probe);
    if (g_default_store &&
        danos_object_read(g_default_store, e->type, e->id,
                          probe, &psz) == DANOS_OK)
        return;   /* alive */

    g_sweep_ids[g_sweep_n].type = e->type;
    g_sweep_ids[g_sweep_n].id = e->id;
    g_sweep_n++;
}

uint64_t danos_programming_sweep(uint64_t *failed)
{
    sweep_ctx_t c = {0, 0};
    if (!g_programmed) {
        if (failed) *failed = 0;
        return 0;
    }
    g_sweep_n = 0;
    danos_object_iterate(g_programmed, sweep_collect, NULL);

    for (unsigned i = 0; i < g_sweep_n; i++) {
        danos_obj_type_t type = g_sweep_ids[i].type;
        danos_obj_id_t id = g_sweep_ids[i].id;

        uint8_t last[520];
        size_t lsz = sizeof(last);
        if (danos_object_read(ledger(), type, id, last, &lsz) != DANOS_OK)
            continue;
        if (lsz < 8 + sizeof(danos_route_t)) continue;

        if (type == DANOS_OBJ_ROUTE && g_ops && g_ops->route_del &&
            lsz >= 8 + sizeof(danos_route_t)) {
            danos_route_t r;
            memcpy(&r, last + 8, sizeof(r));
            danos_resolved_route_t rr;
            memset(&rr, 0, sizeof(rr));
            rr.route = r;
            /* best-effort: nh objects may be gone already; the withdraw
             * only needs prefix/table, which the ledger copy provides */
            danos_status_t st = g_ops->route_del(&rr, g_ops->user);
            if (st != DANOS_OK) {
                c.failed++;
                continue;   /* keep ledger entry: retry next sweep */
            }
        }
        if (type == DANOS_OBJ_IFACE && g_ops && g_ops->iface_up &&
            lsz >= 8 + sizeof(danos_iface_t)) {
            /* withdraw = admin down (we never delete kernel ifaces we
             * did not create) */
            danos_iface_t i;
            memcpy(&i, last + 8, sizeof(i));
            (void)g_ops->iface_up(i.ifindex, false, g_ops->user);
        }
        /* VRF: registration-only backend ops — ledger drop suffices */
        danos_object_delete(ledger(), type, id);
        c.issued++;
    }
    if (failed) *failed = c.failed;
    return c.issued;
}
