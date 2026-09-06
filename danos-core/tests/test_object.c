/* Test: Object Registry (B1) + v0.2 Tunnel/EVPN CRUD */
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int test_object(void)
{
    danos_object_store_t *s = danos_object_store_create(64);
    assert(s != NULL);

    /* Create */
    const char *data = "hello";
    danos_status_t st = danos_object_create(s, DANOS_OBJ_ROUTE, 1, data, 6);
    assert(st == DANOS_OK);

    /* Duplicate create fails */
    st = danos_object_create(s, DANOS_OBJ_ROUTE, 1, data, 6);
    assert(st == DANOS_ERR_EXISTS);

    /* Read */
    char buf[16] = {0};
    size_t sz = sizeof(buf);
    st = danos_object_read(s, DANOS_OBJ_ROUTE, 1, buf, &sz);
    assert(st == DANOS_OK);
    assert(sz == 6);
    assert(strcmp(buf, "hello") == 0);

    /* Update */
    st = danos_object_update(s, DANOS_OBJ_ROUTE, 1, "world", 6);
    assert(st == DANOS_OK);
    sz = sizeof(buf);
    st = danos_object_read(s, DANOS_OBJ_ROUTE, 1, buf, &sz);
    assert(strcmp(buf, "world") == 0);

    /* Count */
    assert(danos_object_count(s, DANOS_OBJ_ROUTE) == 1);
    danos_object_create(s, DANOS_OBJ_ROUTE, 2, "foo", 4);
    assert(danos_object_count(s, DANOS_OBJ_ROUTE) == 2);

    /* Delete */
    st = danos_object_delete(s, DANOS_OBJ_ROUTE, 1);
    assert(st == DANOS_OK);
    st = danos_object_delete(s, DANOS_OBJ_ROUTE, 1);
    assert(st == DANOS_ERR_NOT_FOUND);
    assert(danos_object_count(s, DANOS_OBJ_ROUTE) == 1);

    danos_object_store_destroy(s);
    printf("[PASS] test_object: CRUD works\n");
    return 0;
}

/* v0.2: Tunnel CRUD via DPA public API */
int test_tunnel_crud(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Create VXLAN tunnel */
    danos_tunnel_t tun;
    memset(&tun, 0, sizeof(tun));
    tun.id = 1;
    tun.type = DANOS_TUNNEL_VXLAN;
    tun.ifindex = 100;
    tun.src.af = DANOS_AF_IPV4;
    tun.src.addr[0] = 10; tun.src.addr[3] = 1;  /* 10.0.0.1 */
    tun.dst.af = DANOS_AF_IPV4;
    tun.dst.addr[0] = 10; tun.dst.addr[3] = 2;  /* 10.0.0.2 */
    tun.vni = 10000;

    danos_status_t st = danos_tunnel_create(&tx, &tun);
    assert(st == DANOS_OK);

    /* Read back */
    danos_tunnel_t out;
    st = danos_tunnel_read(&tx, 1, &out);
    assert(st == DANOS_OK);
    assert(out.id == 1);
    assert(out.type == DANOS_TUNNEL_VXLAN);
    assert(out.ifindex == 100);
    assert(out.vni == 10000);

    /* Update */
    tun.vni = 20000;
    st = danos_tunnel_update(&tx, &tun);
    assert(st == DANOS_OK);
    st = danos_tunnel_read(&tx, 1, &out);
    assert(st == DANOS_OK);
    assert(out.vni == 20000);

    /* Delete */
    st = danos_tunnel_delete(&tx, 1);
    assert(st == DANOS_OK);
    st = danos_tunnel_read(&tx, 1, &out);
    assert(st == DANOS_ERR_NOT_FOUND);

    danos_tx_commit(&tx);
    printf("[PASS] test_tunnel_crud: Tunnel create/read/update/delete\n");
    return 0;
}

/* v0.2: EVPN EVI CRUD via DPA public API */
int test_evpn_crud(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Create EVPN EVI */
    danos_evpn_evi_t evi;
    memset(&evi, 0, sizeof(evi));
    evi.evi = 100;
    evi.rd.af = DANOS_AF_IPV4;
    evi.rd.addr[0] = 10; evi.rd.addr[3] = 1;  /* 10.0.0.1 */
    evi.rt_import.af = DANOS_AF_IPV4;
    evi.rt_import.addr[0] = 10; evi.rt_import.addr[3] = 2;
    evi.rt_export.af = DANOS_AF_IPV4;
    evi.rt_export.addr[0] = 10; evi.rt_export.addr[3] = 3;
    evi.vni = 100100;
    evi.irb = true;

    danos_status_t st = danos_evpn_evi_create(&tx, &evi);
    assert(st == DANOS_OK);

    /* Read back */
    danos_evpn_evi_t out;
    st = danos_evpn_evi_read(&tx, 100, &out);
    assert(st == DANOS_OK);
    assert(out.evi == 100);
    assert(out.vni == 100100);
    assert(out.irb == true);

    /* Update */
    evi.irb = false;
    st = danos_evpn_evi_update(&tx, &evi);
    assert(st == DANOS_OK);
    st = danos_evpn_evi_read(&tx, 100, &out);
    assert(st == DANOS_OK);
    assert(out.irb == false);

    /* Delete */
    st = danos_evpn_evi_delete(&tx, 100);
    assert(st == DANOS_OK);
    st = danos_evpn_evi_read(&tx, 100, &out);
    assert(st == DANOS_ERR_NOT_FOUND);

    danos_tx_commit(&tx);
    printf("[PASS] test_evpn_crud: EVPN EVI create/read/update/delete\n");
    return 0;
}

/* v0.2: Multicast mroute CRUD via DPA public API */
int test_mroute_crud(void)
{
    danos_tx_t tx = {0};
    assert(danos_tx_begin(&tx, "test", NULL) == DANOS_OK);

    /* Create (*,G) mroute: vrf=0, group=239.1.1.1, RPF if=1, 2 OIFs */
    danos_mroute_t mr;
    memset(&mr, 0, sizeof(mr));
    mr.vrf_id = 0;
    mr.source.af = DANOS_AF_UNSPEC;          /* (*,G) */
    mr.group.af = DANOS_AF_IPV4;
    mr.group.addr[0] = 239; mr.group.addr[1] = 1;
    mr.group.addr[2] = 1;   mr.group.addr[3] = 1;
    mr.incoming_if = 1;
    mr.oif_count = 2;
    mr.oif_list[0] = 10;
    mr.oif_list[1] = 11;

    danos_status_t st = danos_mroute_create(&tx, &mr);
    assert(st == DANOS_OK);

    /* Read back */
    danos_mroute_t out;
    st = danos_mroute_read(&tx, 0, &mr.group, &out);
    assert(st == DANOS_OK);
    assert(out.vrf_id == 0);
    assert(out.incoming_if == 1);
    assert(out.oif_count == 2);
    assert(out.oif_list[0] == 10);
    assert(out.oif_list[1] == 11);

    /* Update: add a third OIF */
    mr.oif_count = 3;
    mr.oif_list[2] = 12;
    st = danos_mroute_update(&tx, &mr);
    assert(st == DANOS_OK);
    st = danos_mroute_read(&tx, 0, &mr.group, &out);
    assert(st == DANOS_OK);
    assert(out.oif_count == 3);
    assert(out.oif_list[2] == 12);

    /* Delete */
    st = danos_mroute_delete(&tx, 0, &mr.group);
    assert(st == DANOS_OK);
    st = danos_mroute_read(&tx, 0, &mr.group, &out);
    assert(st == DANOS_ERR_NOT_FOUND);

    danos_tx_commit(&tx);
    printf("[PASS] test_mroute_crud: mroute create/read/update/delete\n");
    return 0;
}
