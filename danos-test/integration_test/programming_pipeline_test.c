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

static int test_vpp_adapter_pipeline(void);

int main(void)
{
    if (!g_default_store) g_default_store = danos_object_store_create(256);

    /* mock-mode kernel backend */
    assert(danos_netlink_init(false) == 0);
    danos_netlink_register_backend();
    assert(!danos_netlink_is_real());

    /* ---- 1. desired state via the DPA commit path ---------------------
     * iface + nexthop + nhgroup + route (route references the group;
     * the pipeline resolves gw/oif through it) */
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    danos_iface_t ifc;
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifindex = 5;
    strcpy(ifc.name, "wan0");
    ifc.mtu = 1500;
    ifc.admin_up = true;
    ifc.ipv4_address.addr.af = DANOS_AF_IPV4;
    ifc.ipv4_address.addr.addr[0] = 192;
    ifc.ipv4_address.addr.addr[1] = 0;
    ifc.ipv4_address.addr.addr[2] = 2;
    ifc.ipv4_address.addr.addr[3] = 10;
    ifc.ipv4_address.prefix_len = 24;
    assert(danos_iface_create(&tx, &ifc) == DANOS_OK);

    danos_nexthop_t nh;
    memset(&nh, 0, sizeof(nh));
    nh.id = 100;
    nh.gateway.af = DANOS_AF_IPV4;
    nh.gateway.addr[0] = 192; nh.gateway.addr[1] = 168;
    nh.gateway.addr[2] = 1;   nh.gateway.addr[3] = 1;
    nh.ifindex = 5;
    assert(danos_nh_create(&tx, &nh) == DANOS_OK);

    danos_nhgroup_t grp;
    memset(&grp, 0, sizeof(grp));
    grp.id = 1;
    grp.nh_count = 1;
    grp.nh_ids[0] = 100;
    assert(danos_nhgroup_create(&tx, &grp) == DANOS_OK);

    danos_route_t rt;
    make_route(1, &rt);
    rt.nhgroup_id = 1;
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

    /* Interface address is part of the same desired/programmed lifecycle. */
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    danos_iface_t ifc_update = ifc;
    memset(&ifc_update.ipv4_address, 0, sizeof(ifc_update.ipv4_address));
    assert(danos_iface_update(&tx, &ifc_update) == DANOS_OK);
    danos_tx_commit(&tx);
    attempted = failed = 0;
    assert(danos_programming_run(&attempted, &failed) == 1);
    assert(failed == 0 && attempted == 1);

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

    /* ---- 5b. v0.10 tombstone: delete the desired object -> withdraw ---- */
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    assert(danos_route_delete(&tx, 0, rt.prefix,
                              DANOS_ROUTE_PROTO_STATIC) == DANOS_OK);
    danos_tx_commit(&tx);
    assert(danos_netlink_mock_route_exists(rt.prefix.addr.addr,
                                           rt.prefix.prefix_len, 0));  /* still in kernel */
    uint64_t swept = danos_programming_sweep(&failed);
    assert(swept == 1 && failed == 0);
    assert(!danos_netlink_mock_route_exists(rt.prefix.addr.addr,
                                            rt.prefix.prefix_len, 0));
    assert(danos_netlink_mock_route_count() == 1);

    /* ---- 5c. v0.11 NH drift: gateway change must re-program ------------ */
    {
        danos_nexthop_t nh2;
        memset(&nh2, 0, sizeof(nh2));
        nh2.id = 100;
        nh2.gateway.af = DANOS_AF_IPV4;
        nh2.gateway.addr[0] = 192; nh2.gateway.addr[1] = 168;
        nh2.gateway.addr[2] = 1;   nh2.gateway.addr[3] = 254;   /* new gw */
        nh2.ifindex = 5;
        assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
        assert(danos_nh_update(&tx, &nh2) == DANOS_OK);
        danos_tx_commit(&tx);
    }
    attempted = failed = 0;
    assert(danos_programming_run(&attempted, &failed) == 1);   /* re-programmed! */
    assert(failed == 0);

    /* ---- 5d. v0.11 NH deletion: route withdrawn, not stale -------------- */
    {
        assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
        assert(danos_object_delete(g_default_store, DANOS_OBJ_NEXTHOP, 100)
               == DANOS_OK);
        danos_tx_commit(&tx);
    }
    attempted = failed = 0;
    (void)danos_programming_run(&attempted, &failed);
    assert(failed == 0);
    assert(danos_netlink_mock_route_count() == 0);   /* withdrawn */
    assert(danos_programming_programmed_count(DANOS_OBJ_ROUTE) == 0);

    /* ---- 5e. v0.11 iface tombstone: delete -> admin down ---------------- */
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    assert(danos_iface_delete(&tx, 5) == DANOS_OK);
    danos_tx_commit(&tx);
    uint64_t swept2 = danos_programming_sweep(&failed);
    assert(swept2 >= 1 && failed == 0);
    assert(!danos_netlink_mock_iface_up(5));

    /* restore for the remaining assertions */
    {
        danos_tx_t t2;
        assert(danos_tx_begin(&t2, "pipe", NULL) == DANOS_OK);
        danos_iface_t i2;
        memset(&i2, 0, sizeof(i2));
        i2.ifindex = 5;
        strcpy(i2.name, "wan0");
        i2.mtu = 1500; i2.admin_up = true;
        assert(danos_iface_create(&t2, &i2) == DANOS_OK);
        danos_tx_commit(&t2);
    }

    /* ---- 6. reconciler honesty ----------------------------------------- */
    /* first sync the restored interface, then verify a quiet pass */
    (void)danos_programming_run(&attempted, &failed);
    danos_reconcile_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    assert(danos_reconciler_init(NULL, &cfg) == 0);   /* programming-only */
    uint64_t diffs = danos_reconciler_run_once();     /* everything in sync */
    assert(diffs == 0);
    danos_reconcile_stats_t stats;
    assert(danos_reconcile_get_stats(DANOS_OBJ_IFACE, &stats) == DANOS_OK);
    (void)stats;

    /* un-programmed change through run_once must be attempted + counted
     * (recreate the NH objects deleted in step 5d) */
    make_route(2, &rt);
    assert(danos_tx_begin(&tx, "pipe", NULL) == DANOS_OK);
    danos_nexthop_t nh3;
    memset(&nh3, 0, sizeof(nh3));
    nh3.id = 100;
    nh3.gateway.af = DANOS_AF_IPV4;
    nh3.gateway.addr[0] = 192; nh3.gateway.addr[1] = 168;
    nh3.gateway.addr[2] = 1;   nh3.gateway.addr[3] = 1;
    nh3.ifindex = 5;
    assert(danos_nh_create(&tx, &nh3) == DANOS_OK);
    /* nhgroup 1 still exists in desired state */
    assert(danos_route_create(&tx, &rt) == DANOS_OK);
    danos_tx_commit(&tx);
    diffs = danos_reconciler_run_once();
    /* two routes programmed: 10.2 (new) and 10.1 (pending RETRY from
     * the NH deletion — now resolvable again). The pending-retry
     * recovery is itself a v0.11 behavior worth asserting. */
    assert(diffs == 2);
    assert(danos_netlink_mock_route_count() == 2);

    danos_reconciler_fini();

    assert(test_vpp_adapter_pipeline() == 0);
    danos_netlink_shutdown();

    printf("=== programming_pipeline_test: ALL PASSED ===\n");
    return 0;
}

/* ---- P2: the SAME pipeline drives the VPP adapter (mock mode) ---------- */
#include "../../danos-vpp/src/api/vpp_api.h"
#include "../../danos-vpp/src/vpp_adapter.h"

static int test_vpp_adapter_pipeline(void)
{
    /* install VPP adapter (mock backend) */
    assert(danos_vpp_adapter_install(false, NULL) == 0);
    assert(strcmp(danos_backend_ops_get()->name, "vpp") == 0);

    /* desired: a route through NH group 1 (NH recreated in the NH test) */
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "vpp", NULL) == DANOS_OK);
    danos_route_t rt;
    memset(&rt, 0, sizeof(rt));
    rt.vrf_id = 0;
    rt.prefix.addr.af = DANOS_AF_IPV4;
    rt.prefix.addr.addr[0] = 172; rt.prefix.addr.addr[1] = 16;
    rt.prefix.prefix_len = 16;
    rt.protocol = DANOS_ROUTE_PROTO_STATIC;
    rt.nhgroup_id = 1;
    assert(danos_route_create(&tx, &rt) == DANOS_OK);
    danos_tx_commit(&tx);

    uint64_t attempted = 0, failed = 0;
    uint64_t ok = danos_programming_run(&attempted, &failed);
    assert(ok >= 1 && failed == 0);

    /* the VPP binary API saw the ip_route_add_del message */
    uint64_t sent = 0, recvd = 0, conns = 0, reconns = 0;
    danos_vpp_api_get_stats(&sent, &recvd, &conns, &reconns);
    assert(sent >= 1);

    /* switch back to netlink for consistency */
    danos_netlink_register_backend();
    printf("[PASS] test_vpp_adapter_pipeline (same pipeline, second backend)\n");
    return 0;
}
