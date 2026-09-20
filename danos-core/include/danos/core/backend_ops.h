/*
 * DANOS-Open Core: Backend Programming Interface (v0.9, ADR-0007)
 *
 * The contract between the desired-state store and dataplane backends
 * (kernel netlink, VPP, ...). The programming pipeline calls these ops
 * for every DESIRED object that is not yet PROGRAMMED; success moves
 * the object to PROGRAMMED.
 *
 * Rules:
 *   - Ops must be idempotent (reconciler may re-issue them).
 *   - Ops may block briefly (netlink round trip) but not indefinitely.
 *   - Ops are called from the reconciler/programming thread only.
 */

#ifndef DANOS_BACKEND_OPS_H__
#define DANOS_BACKEND_OPS_H__

#include <danos/dpa.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A route with its next-hop resolved by the pipeline (gateway and
 * egress interface pulled from the referenced NH group's first member). */
typedef struct {
    danos_route_t route;        /* copy of the desired object */
    bool        has_gw;         /* gateway present */
    uint8_t     gw[16];         /* gateway address (v4 in first 4 bytes) */
    uint32_t    oif;            /* egress ifindex (0 = unresolved) */
} danos_resolved_route_t;

typedef struct danos_backend_ops {
    const char *name;                 /* "netlink", "vpp", "mock", ... */

    /* Interface admin state. ifindex is the DPA object id. */
    danos_status_t (*iface_up)(danos_ifindex_t ifindex, bool up, void *user);
    /* Program the primary interface addresses, if present. */
    danos_status_t (*iface_addr_set)(const danos_iface_t *iface, void *user);
    danos_status_t (*iface_addr_del)(const danos_iface_t *iface, void *user);

    /* Routes: nhgroup_id == 0 without BLACKHOLE flag means the route
     * carries no usable path yet — the pipeline leaves it unprogrammed
     * and retried. */
    danos_status_t (*route_add)(const danos_resolved_route_t *route, void *user);
    danos_status_t (*route_del)(const danos_resolved_route_t *route, void *user);

    danos_status_t (*vrf_add)(const danos_vrf_t *vrf, void *user);
    danos_status_t (*vrf_del)(danos_vrf_id_t vrf_id, void *user);

    void *user;
} danos_backend_ops_t;

/* Install the active backend ops (NULL disables programming).
 * Called once at daemon startup, before the reconciler starts. */
void danos_backend_ops_set(const danos_backend_ops_t *ops);

/* Currently installed ops (NULL if none). */
const danos_backend_ops_t *danos_backend_ops_get(void);

/* One programming pass over the desired-state (default) store.
 * Returns the number of objects newly programmed/updated;
 * attempted/failed counters are optional outs. */
uint64_t danos_programming_run(uint64_t *attempted, uint64_t *failed);

/* Number of objects currently marked PROGRAMMED for a type. */
uint64_t danos_programming_programmed_count(danos_obj_type_t type);

/* Mark desired state dirty (e.g. on store mutation): the next
 * reconciler pass runs immediately instead of waiting for the period. */
void danos_programming_mark_dirty(void);

/* Consume the dirty flag (reconciler thread). Returns 1 if set. */
int danos_programming_dirty_take(void);

/* Cumulative programming counters (v0.13, Prometheus-visible). */
void danos_programming_get_stats(uint64_t *attempted, uint64_t *ok,
                                 uint64_t *failed);

/* Reconcile the backend against desired state: ledger entries whose
 * desired object disappeared are deleted via ops->route_del (v0.10
 * tombstone tracking, routes only). Returns deletions issued. */
uint64_t danos_programming_sweep(uint64_t *failed);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_BACKEND_OPS_H__ */
