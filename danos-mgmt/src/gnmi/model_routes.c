/*
 * DANOS-Open Management: Composite Route Management implementation (v0.12)
 */

#include "model_routes.h"
#include <danos/core/object_registry.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* forward declarations (definitions appear later in the file) */
static void nh_max_iter(danos_object_entry_t *e, void *user);
static void group_max_iter(danos_object_entry_t *e, void *user);
static danos_ip_prefix_t g_match_prefix;
static danos_vrf_id_t g_match_vrf;

danos_status_t gnmi_parse_ipv4(const char *s, danos_ip_addr_t *out)
{
    if (!s || !out) return DANOS_ERR_INVALID_ARG;
    unsigned a, b, c, d;
    char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return DANOS_ERR_INVALID_ARG;
    if (a > 255 || b > 255 || c > 255 || d > 255) return DANOS_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->af = DANOS_AF_IPV4;
    out->addr[0] = (uint8_t)a;
    out->addr[1] = (uint8_t)b;
    out->addr[2] = (uint8_t)c;
    out->addr[3] = (uint8_t)d;
    return DANOS_OK;
}

danos_status_t gnmi_parse_prefix(const char *s, danos_ip_prefix_t *out)
{
    if (!s || !out) return DANOS_ERR_INVALID_ARG;
    char ipbuf[64];
    strncpy(ipbuf, s, sizeof(ipbuf) - 1);
    ipbuf[sizeof(ipbuf) - 1] = '\0';
    char *slash = strchr(ipbuf, '/');
    if (!slash) return DANOS_ERR_INVALID_ARG;
    *slash = '\0';
    unsigned len = (unsigned)strtoul(slash + 1, NULL, 10);
    if (len > 32) return DANOS_ERR_INVALID_ARG;
    danos_status_t st = gnmi_parse_ipv4(ipbuf, &out->addr);
    if (st != DANOS_OK) return st;
    out->prefix_len = (uint8_t)len;
    return DANOS_OK;
}

/* ---- object discovery helpers ------------------------------------------- */

typedef struct {
    danos_vrf_id_t vrf;
    danos_ip_prefix_t prefix;
    danos_obj_id_t route_id;
    danos_obj_id_t group_id;
    danos_obj_id_t nh_id;
    bool found;
} route_lookup_t;

static bool prefix_equal(const danos_ip_prefix_t *a, const danos_ip_prefix_t *b)
{
    return a->prefix_len == b->prefix_len &&
           a->addr.af == b->addr.af &&
           memcmp(a->addr.addr, b->addr.addr, 16) == 0;
}

static void lookup_iter(danos_object_entry_t *e, void *user)
{
    route_lookup_t *lu = user;
    if (lu->found) return;
    if (e->type != DANOS_OBJ_ROUTE || e->data_size < sizeof(danos_route_t))
        return;
    const danos_route_t *r = e->data;
    if (r->vrf_id != lu->vrf) return;
    if (r->protocol != DANOS_ROUTE_PROTO_STATIC) return;
    if (!prefix_equal(&r->prefix, &lu->prefix)) return;

    lu->found = true;
    lu->route_id = e->id;
    lu->group_id = r->nhgroup_id;
    /* resolve first NH of the group */
    danos_nhgroup_t grp;
    size_t sz = sizeof(grp);
    if (lu->group_id &&
        danos_object_read(g_default_store, DANOS_OBJ_NHGROUP, lu->group_id,
                          &grp, &sz) == DANOS_OK && grp.nh_count > 0) {
        lu->nh_id = grp.nh_ids[0];
    }
}

static void find_static_route(danos_vrf_id_t vrf,
                              const danos_ip_prefix_t *prefix,
                              route_lookup_t *lu)
{
    memset(lu, 0, sizeof(*lu));
    lu->vrf = vrf;
    lu->prefix = *prefix;
    danos_object_iterate(g_default_store, lookup_iter, lu);
}

/* next free NH id: max+1 (single store, single writer per tx) */
static danos_obj_id_t next_nh_id(void)
{
    danos_obj_id_t max = 100;   /* reserve <100 for static configs */
    danos_object_iterate(g_default_store, nh_max_iter, &max);
    return max + 1;
}

static void nh_max_iter(danos_object_entry_t *e, void *user)
{
    if (e->type != DANOS_OBJ_NEXTHOP) return;
    if (e->id > *(danos_obj_id_t *)user) *(danos_obj_id_t *)user = e->id;
}

static void group_max_iter(danos_object_entry_t *e, void *user)
{
    if (e->type != DANOS_OBJ_NHGROUP) return;
    if (e->id > *(danos_obj_id_t *)user) *(danos_obj_id_t *)user = e->id;
}

static danos_obj_id_t next_group_id(void)
{
    danos_obj_id_t max = 100;
    danos_object_iterate(g_default_store, group_max_iter, &max);
    return max + 1;
}

/* ---- composite set -------------------------------------------------------- */

danos_status_t gnmi_route_set(danos_vrf_id_t vrf_id,
                              const danos_ip_prefix_t *prefix,
                              const danos_ip_addr_t *gateway,
                              uint32_t oif)
{
    if (!prefix || !gateway || gateway->af != DANOS_AF_IPV4)
        return DANOS_ERR_INVALID_ARG;
    if (!g_default_store) return DANOS_ERR_INVALID_ARG;

    route_lookup_t lu;
    find_static_route(vrf_id, prefix, &lu);

    danos_tx_t tx;
    danos_status_t st = danos_tx_begin(&tx, "gnmi-route", NULL);
    if (st != DANOS_OK) return st;

    danos_obj_id_t nh_id, group_id;
    if (lu.found) {
        /* update path: mutate the existing NH in place */
        nh_id = lu.nh_id;
        group_id = lu.group_id;
        danos_nexthop_t nh;
        memset(&nh, 0, sizeof(nh));
        nh.id = nh_id;
        nh.gateway = *gateway;
        nh.ifindex = oif;
        st = danos_nh_update(&tx, &nh);
        if (st != DANOS_OK) { danos_tx_abort(&tx); return st; }
    } else {
        /* create path: NH -> group -> route */
        nh_id = next_nh_id();
        group_id = next_group_id();
        danos_nexthop_t nh;
        memset(&nh, 0, sizeof(nh));
        nh.id = nh_id;
        nh.gateway = *gateway;
        nh.ifindex = oif;
        st = danos_nh_create(&tx, &nh);
        if (st == DANOS_OK) {
            danos_nhgroup_t grp;
            memset(&grp, 0, sizeof(grp));
            grp.id = group_id;
            grp.nh_count = 1;
            grp.nh_ids[0] = nh_id;
            st = danos_nhgroup_create(&tx, &grp);
        }
        if (st == DANOS_OK) {
            danos_route_t r;
            memset(&r, 0, sizeof(r));
            r.vrf_id = vrf_id;
            r.prefix = *prefix;
            r.protocol = DANOS_ROUTE_PROTO_STATIC;
            r.admin_distance = 1;
            r.metric = 0;
            r.nhgroup_id = group_id;
            st = danos_route_create(&tx, &r);
        }
    }

    if (st != DANOS_OK) { danos_tx_abort(&tx); return st; }
    st = danos_tx_prepare(&tx);
    if (st == DANOS_OK) st = danos_tx_validate(&tx);
    if (st == DANOS_OK) st = danos_tx_commit(&tx);
    if (st != DANOS_OK) danos_tx_abort(&tx);
    return st;
}

/* ---- composite delete ------------------------------------------------------ */

typedef struct {
    danos_obj_id_t route_id, group_id;
    danos_obj_id_t nh_ids[16];
    uint32_t nh_count;
} cascade_t;

static void cascade_collect(danos_object_entry_t *e, void *user)
{
    cascade_t *c = user;
    if (e->type != DANOS_OBJ_ROUTE || e->data_size < sizeof(danos_route_t))
        return;
    const danos_route_t *r = e->data;
    if (r->protocol != DANOS_ROUTE_PROTO_STATIC) return;
    if (r->vrf_id != g_match_vrf) return;
    if (!prefix_equal(&r->prefix, &g_match_prefix)) return;
    if (c->route_id) return;
    c->route_id = e->id;
    c->group_id = r->nhgroup_id;
}

danos_status_t gnmi_route_delete(danos_vrf_id_t vrf_id,
                                 const danos_ip_prefix_t *prefix)
{
    if (!prefix || !g_default_store) return DANOS_ERR_INVALID_ARG;

    cascade_t c;
    memset(&c, 0, sizeof(c));
    g_match_prefix = *prefix;
    g_match_vrf = vrf_id;
    danos_object_iterate(g_default_store, cascade_collect, &c);
    if (!c.route_id) return DANOS_OK;   /* idempotent delete */

    /* resolve group members BEFORE deleting the route */
    danos_nhgroup_t grp;
    size_t sz = sizeof(grp);
    bool have_group = c.group_id &&
        danos_object_read(g_default_store, DANOS_OBJ_NHGROUP, c.group_id,
                          &grp, &sz) == DANOS_OK;

    danos_tx_t tx;
    danos_status_t st = danos_tx_begin(&tx, "gnmi-route-del", NULL);
    if (st != DANOS_OK) return st;

    /* order: route first (references the group), then group, then NHs */
    st = danos_object_delete(g_default_store, DANOS_OBJ_ROUTE, c.route_id);
    if (st == DANOS_OK && have_group) {
        st = danos_object_delete(g_default_store, DANOS_OBJ_NHGROUP,
                                 c.group_id);
    }
    if (st == DANOS_OK && have_group) {
        for (uint32_t i = 0; i < grp.nh_count && i < 16; i++) {
            (void)danos_object_delete(g_default_store, DANOS_OBJ_NEXTHOP,
                                      grp.nh_ids[i]);
        }
        st = DANOS_OK;
    }
    if (st != DANOS_OK) { danos_tx_abort(&tx); return st; }
    st = danos_tx_prepare(&tx);
    if (st == DANOS_OK) st = danos_tx_validate(&tx);
    if (st == DANOS_OK) st = danos_tx_commit(&tx);
    if (st != DANOS_OK) danos_tx_abort(&tx);
    return st;
}

/* ---- render ---------------------------------------------------------------- */

danos_status_t gnmi_route_to_json(const danos_route_t *route,
                                  char *out, size_t cap)
{
    char pfx[64];
    if (route->prefix.addr.af == DANOS_AF_IPV4) {
        snprintf(pfx, sizeof(pfx), "%u.%u.%u.%u/%u",
                 route->prefix.addr.addr[0], route->prefix.addr.addr[1],
                 route->prefix.addr.addr[2], route->prefix.addr.addr[3],
                 route->prefix.prefix_len);
    } else {
        snprintf(pfx, sizeof(pfx), "<ipv6>/%u", route->prefix.prefix_len);
    }

    /* resolve gateway/oif via the group's first NH */
    char gw[64] = "none";
    uint32_t oif = 0;
    if (route->nhgroup_id) {
        danos_nhgroup_t grp;
        size_t sz = sizeof(grp);
        if (route->nhgroup_id &&
            danos_object_read(g_default_store, DANOS_OBJ_NHGROUP,
                              route->nhgroup_id, &grp, &sz) == DANOS_OK &&
            grp.nh_count > 0) {
            danos_nexthop_t nh;
            sz = sizeof(nh);
            if (danos_object_read(g_default_store, DANOS_OBJ_NEXTHOP,
                                  grp.nh_ids[0], &nh, &sz) == DANOS_OK &&
                nh.gateway.af == DANOS_AF_IPV4) {
                snprintf(gw, sizeof(gw), "%u.%u.%u.%u",
                         nh.gateway.addr[0], nh.gateway.addr[1],
                         nh.gateway.addr[2], nh.gateway.addr[3]);
                oif = nh.ifindex;
            }
        }
    }

    snprintf(out, cap,
             "{\"prefix\":\"%s\",\"vrf\":%u,\"gateway\":\"%s\",\"oif\":%u}",
             pfx, route->vrf_id, gw, oif);
    return DANOS_OK;
}
