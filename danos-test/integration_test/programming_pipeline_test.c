/*
 * v0.9 integration: the programming pipeline end to end (in-process).
 *
 *   1. DPA CRUD (the gNMI/CLI/NETCONF commit path) writes desired state
 *   2. danos_programming_run() drives the netlink adapter
 *   3. mock kernel FIB assertions (route present, iface up)
 *   4. idempotency: second run is a no-op
 *   5. drift: object update -> next run re-programs
 *   6. reconciler honesty: run_once() counts real attempts/failures
 */

#include <danos/core/backend_ops.h>
#include <danos/core/object_registry.h>
#include <danos/core/reconciler.h>
#include "../danos-netlink/danos_netlink.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static void make_route(unsigned i, danos_route_t *r)
{
    memset(r, 0, sizeof(*r));
    r->vrf_id = 0;
    r->prefix.addr.af = DANOS_AF_IPV4;
    r->prefix.addr.addr[0] = 10;
    r->prefix.addr.addr[1] = (uint8_t)i;
    r->prefix.addr.addr[3] = 0;
    r->prefix.prefix_len = 24;
    r->protocol = DANOS_ROUTE_PROTO_STATIC;
    r->nhgroup_id = 1;   /* a usable path */
}

int main(void)
{
    if (!g_default_store) g_default_store = danos_object_store_create(256);

    /* mock-mode kernel backend */
    assert(danos_netlink_init(false) == 0);
    danos_netlink_register_backend();
    assert(!danos_netlink_is_real());

    /* ---- 1. desired state via the DPA commit path --------------------- */
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    danos_iface_t ifc;
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifindex = 5;
    strcpy(ifc.name, "wan0");
    ifc.mtu = 1500;
    ifc.admin_up = true;
    assert(danos_iface_create(&tx, &ifc) == DANOS_OK);

    danos_route_t rt;
    make_route(1, &rt);
    assert(danos_route_create(&tx, &rt) == DANOS_OK);
    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);

    /* ---- 2. program the pipeline -------------------------------------- */
    uint64_t attempted = 0, failed = 0;
    uint64_t programmed = danos_programming_run(&attempted, &failed);
    assert(programmed == 2);          /* iface + route */
    assert(failed == 0);
    assert(attempted == 2);

    /* ---- 3. mock kernel assertions ------------------------------------- */
    assert(danos_netlink_mock_route_count() == 1);
    assert(danos_netlink_mock_route_exists(rt.prefix.addr.addr,
                                           rt.prefix.prefix_len, 0));
    assert(danos_netlink_mock_iface_up(5));

    /* ---- 4. idempotency ------------------------------------------------ */
    attempted = failed = 0;
    assert(danos_programming_run(&attempted, &failed) == 0);
    assert(attempted == 0);

    /* ---- 5. drift: update the route -> re-program ---------------------- */
    make_route(1, &rt);
    rt.prefix.addr.addr[1] = 99;    /* 10.99.0.0/24 */
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    assert(danos_route_create(&tx, &rt) == DANOS_OK);
    danos_tx_commit(&tx);

    programmed = danos_programming_run(&attempted, &failed);
    assert(programmed == 1 && failed == 0);
    assert(danos_netlink_mock_route_count() == 2);
    assert(danos_netlink_mock_route_exists(rt.prefix.addr.addr,
                                           rt.prefix.prefix_len, 0));

    /* ---- 6. reconciler honesty ----------------------------------------- */
    danos_reconcile_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    assert(danos_reconciler_init(NULL, &cfg) == 0);   /* programming-only */
    uint64_t diffs = danos_reconciler_run_once();     /* everything in sync */
    assert(diffs == 0);
    danos_reconcile_stats_t stats;
    assert(danos_reconcile_get_stats(DANOS_OBJ_IFACE, &stats) == DANOS_OK);
    (void)stats;

    /* un-programmed change through run_once must be attempted + counted */
    make_route(2, &rt);
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    assert(danos_route_create(&tx, &rt) == DANOS_OK);
    danos_tx_commit(&tx);
    diffs = danos_reconciler_run_once();
    assert(diffs == 1);   /* the new route was actually programmed */
    assert(danos_netlink_mock_route_count() == 3);

    danos_reconciler_fini();
    danos_netlink_shutdown();

    printf("=== programming_pipeline_test: ALL PASSED ===\n");
    return 0;
}
