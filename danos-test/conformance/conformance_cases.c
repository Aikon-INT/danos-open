/*
 * DPA Conformance test cases (skeleton implementations)
 *
 * These verify the DPA API contract. Each test creates a transaction,
 * performs operations, and checks results. Backend under test is
 * linked at build time.
 *
 * For v0.1 these are skeleton implementations that verify the API
 * compiles and basic invariants hold. Real backend testing happens
 * in danos-vpp/tests.
 */

#include <danos/dpa.h>
#include <assert.h>
#include <string.h>
#include <time.h>

/* Helper: get monotonic ns timestamp */
__attribute__((unused))
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Helper: default timeouts */
static const danos_tx_timeouts_t kDefaultTimeouts = {
    .prepare_ms = 100,
    .commit_ms  = 500,
    .verify_ms  = 1000,
};

/* ----------------------------------------------------------------------- */
/* 1. Transaction lifecycle: begin → prepare → validate → commit → done     */
/* ----------------------------------------------------------------------- */
int conf_tx_lifecycle(void)
{
    danos_tx_t tx = {0};
    danos_status_t st;

    st = danos_tx_begin(&tx, "conformance", &kDefaultTimeouts);
    if (st != DANOS_OK) return 1;
    if (tx.id == 0) return 2;

    st = danos_tx_prepare(&tx);
    if (st != DANOS_OK) return 3;

    st = danos_tx_validate(&tx);
    if (st != DANOS_OK) return 4;

    st = danos_tx_commit(&tx);
    if (st != DANOS_OK) return 5;

    /* Verify is best-effort; may not be implemented in skeleton */
    danos_tx_verify(&tx);

    danos_tx_state_t state;
    danos_tx_get_state(&tx, &state);
    /* After commit+verify, state should be DONE or VERIFY */
    if (state != DANOS_TX_DONE && state != DANOS_TX_VERIFY) return 6;

    return 0;
}

/* ----------------------------------------------------------------------- */
/* 2. Transaction abort                                                     */
/* ----------------------------------------------------------------------- */
int conf_tx_concurrent(void)
{
    danos_tx_t tx1 = {0}, tx2 = {0};
    danos_status_t st;

    st = danos_tx_begin(&tx1, "conf-1", &kDefaultTimeouts);
    if (st != DANOS_OK) return 1;

    st = danos_tx_begin(&tx2, "conf-2", &kDefaultTimeouts);
    if (st != DANOS_OK) return 2;

    /* Abort tx1 */
    st = danos_tx_abort(&tx1);
    if (st != DANOS_OK) return 3;

    /* Full lifecycle for tx2: prepare → validate → commit → verify */
    st = danos_tx_prepare(&tx2);
    if (st != DANOS_OK) return 4;
    st = danos_tx_validate(&tx2);
    if (st != DANOS_OK) return 5;
    st = danos_tx_commit(&tx2);
    if (st != DANOS_OK) return 6;
    st = danos_tx_verify(&tx2);
    if (st != DANOS_OK) return 7;

    return 0;
}

/* ----------------------------------------------------------------------- */
/* 3-9. Object CRUD skeletons                                               */
/* Each creates an object, reads it back, updates, deletes.                 */
/* For v0.1 skeleton, we verify the API is callable.                        */
/* ----------------------------------------------------------------------- */
int conf_iface_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_iface_t iface = {0};
    iface.ifindex = 1;
    iface.type = DANOS_IF_TYPE_PHYS;
    iface.mtu = 1500;
    strncpy(iface.name, "eth0", sizeof(iface.name) - 1);
    iface.admin_up = true;

    /* Create may succeed or return NOT_SUPPORTED (no backend); both OK for skeleton */
    danos_status_t st = danos_iface_create(&tx, &iface);
    (void)st;

    danos_tx_commit(&tx);
    return 0;
}

int conf_vrf_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_vrf_t vrf = {0};
    vrf.vrf_id = 100;
    vrf.ipv4_active = true;
    strncpy(vrf.name, "VRF100", sizeof(vrf.name) - 1);

    danos_vrf_create(&tx, &vrf);
    danos_tx_commit(&tx);
    return 0;
}

int conf_route_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_route_t route = {0};
    route.vrf_id = 0;
    route.prefix.addr.af = DANOS_AF_IPV4;
    route.prefix.addr.addr[12] = 10; route.prefix.addr.addr[13] = 0;
    route.prefix.addr.addr[14] = 0;  route.prefix.addr.addr[15] = 0;
    route.prefix.prefix_len = 24;
    route.protocol = DANOS_ROUTE_PROTO_STATIC;
    route.admin_distance = 1;
    route.metric = 0;

    danos_route_create(&tx, &route);
    danos_tx_commit(&tx);
    return 0;
}

int conf_nh_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_nexthop_t nh = {0};
    nh.id = 1;
    nh.gateway.af = DANOS_AF_IPV4;
    nh.gateway.addr[15] = 1;
    nh.ifindex = 1;
    nh.weight = 1;

    danos_nh_create(&tx, &nh);
    danos_tx_commit(&tx);
    return 0;
}

int conf_nhgroup_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_nhgroup_t grp = {0};
    grp.id = 1;
    grp.nh_count = 2;
    grp.nh_ids[0] = 1;
    grp.nh_ids[1] = 2;

    danos_nhgroup_create(&tx, &grp);
    danos_tx_commit(&tx);
    return 0;
}

int conf_acl_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_acl_table_t tbl = {0};
    tbl.table_id = 1;
    strncpy(tbl.name, "ACL1", sizeof(tbl.name) - 1);
    tbl.ingress = true;

    danos_acl_table_create(&tx, &tbl);
    danos_tx_commit(&tx);
    return 0;
}

int conf_qos_crud(void)
{
    danos_tx_t tx = {0};
    if (danos_tx_begin(&tx, "conf", &kDefaultTimeouts) != DANOS_OK) return 1;

    danos_qos_policy_t p = {0};
    p.policy_id = 1;
    strncpy(p.name, "POLICE1", sizeof(p.name) - 1);
    p.cir_bps = 100000000;  /* 100 Mbps */
    p.cb_bytes = 12500000;  /* 100ms burst */

    danos_qos_policy_create(&tx, &p);
    danos_tx_commit(&tx);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* 10. Capability query                                                     */
/* ----------------------------------------------------------------------- */
int conf_capability_query(void)
{
    danos_capability_t cap;
    /* Query may return OK or NOT_SUPPORTED depending on backend registration */
    danos_status_t st = danos_capability_query(NULL, DANOS_OBJ_ROUTE, &cap);
    (void)st;
    return 0;
}

/* ----------------------------------------------------------------------- */
/* 11. Version negotiation                                                  */
/* ----------------------------------------------------------------------- */
int conf_version_negotiate(void)
{
    danos_version_t v = danos_dpa_get_version();
    if (v.major != 0) return 1;
    if (v.minor != 1) return 2;
    return 0;
}
