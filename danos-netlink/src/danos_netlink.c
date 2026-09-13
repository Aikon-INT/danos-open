/*
 * DANOS-Open Backend: Linux kernel netlink adapter implementation.
 */

#include "danos_netlink.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>

/* =========================================================================
 * Shared state
 * ========================================================================= */

static struct {
    bool real;
    bool initialized;
} g_nl;

/* =========================================================================
 * Mock tables (in-memory FIB / iface flags)
 * ========================================================================= */

#define MOCK_MAX_ROUTES 4096
#define MOCK_MAX_IFACES 256

typedef struct {
    uint32_t table_id;
    uint8_t  addr[4];
    uint8_t  len;
    bool     used;
} mock_route_t;

static struct {
    mock_route_t routes[MOCK_MAX_ROUTES];
    unsigned route_count;
    bool iface_up[MOCK_MAX_IFACES];
    bool iface_known[MOCK_MAX_IFACES];
} g_mock;

static mock_route_t *mock_find(const uint8_t addr[4], uint8_t len,
                               danos_vrf_id_t table_id)
{
    for (unsigned i = 0; i < MOCK_MAX_ROUTES; i++) {
        mock_route_t *r = &g_mock.routes[i];
        if (r->used && r->len == len && r->table_id == table_id &&
            memcmp(r->addr, addr, 4) == 0)
            return r;
    }
    return NULL;
}

static mock_route_t *mock_alloc(void)
{
    for (unsigned i = 0; i < MOCK_MAX_ROUTES; i++)
        if (!g_mock.routes[i].used) return &g_mock.routes[i];
    return NULL;
}

unsigned danos_netlink_mock_route_count(void)
{
    return g_mock.route_count;
}

bool danos_netlink_mock_route_exists(const uint8_t addr[4], uint8_t len,
                                     danos_vrf_id_t table_id)
{
    return mock_find(addr, len, table_id) != NULL;
}

bool danos_netlink_mock_iface_up(danos_ifindex_t ifindex)
{
    if (ifindex == 0 || ifindex >= MOCK_MAX_IFACES) return false;
    return g_mock.iface_known[ifindex] && g_mock.iface_up[ifindex];
}

bool danos_netlink_is_real(void)
{
    return g_nl.real && g_nl.initialized;
}

/* =========================================================================
 * Real rtnetlink
 * ========================================================================= */

static int nl_fd = -1;

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

static int nl_open(void)
{
    if (nl_fd >= 0) return 0;
    nl_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    return nl_fd < 0 ? -1 : 0;
}

/* Build and send RTM_NEWROUTE for an IPv4 route (kernel table_id). */
static int nl_route_msg(const danos_route_t *r, int add)
{
    struct {
        struct nlmsghdr nh;
        struct rtmsg    rt;
        char            attrbuf[256];
    } msg;
    memset(&msg, 0, sizeof(msg));

    msg.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    msg.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | (add ? NLM_F_CREATE : 0);
    msg.nh.nlmsg_type = add ? RTM_NEWROUTE : RTM_DELROUTE;
    msg.nh.nlmsg_seq = 1;

    msg.rt.rtm_family = AF_INET;
    msg.rt.rtm_dst_len = r->prefix.prefix_len;
    msg.rt.rtm_table = r->vrf_id < 256 ? r->vrf_id : RT_TABLE_MAIN;
    msg.rt.rtm_type = RTN_UNICAST;
    msg.rt.rtm_protocol = RTPROT_STATIC;

    struct rtattr *rta = (struct rtattr *)msg.attrbuf;
    /* RTA_DST */
    rta->rta_type = RTA_DST;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), r->prefix.addr.addr, 4);
    msg.nh.nlmsg_len += RTA_LENGTH(4);

    if (send(nl_fd, &msg, msg.nh.nlmsg_len, 0) < 0) return -1;

    /* read ACK (or error) */
    char resp[512];
    ssize_t n = recv(nl_fd, resp, sizeof(resp), 0);
    if (n < 0) return -1;
    struct nlmsghdr *nh = (struct nlmsghdr *)resp;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
        return err->error == 0 ? 0 : -1;
    }
    return 0;
}

static int nl_iface_up(danos_ifindex_t ifindex, bool up)
{
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } msg;
    memset(&msg, 0, sizeof(msg));
    msg.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    msg.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    msg.nh.nlmsg_type = RTM_NEWLINK;
    msg.nh.nlmsg_seq = 1;
    msg.ifi.ifi_index = (int)ifindex;
    msg.ifi.ifi_flags = up ? IFF_UP : 0;
    msg.ifi.ifi_change = IFF_UP;

    if (send(nl_fd, &msg, msg.nh.nlmsg_len, 0) < 0) return -1;
    char resp[512];
    ssize_t n = recv(nl_fd, resp, sizeof(resp), 0);
    if (n < 0) return -1;
    struct nlmsghdr *nh = (struct nlmsghdr *)resp;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
        return err->error == 0 ? 0 : -1;
    }
    return 0;
}

/* =========================================================================
 * danos_backend_ops_t implementation
 * ========================================================================= */

static danos_status_t ops_iface_up(danos_ifindex_t ifindex, bool up, void *user)
{
    (void)user;
    if (ifindex == 0) return DANOS_ERR_INVALID_ARG;
    if (!g_nl.real) {
        if (ifindex >= MOCK_MAX_IFACES) return DANOS_ERR_INVALID_ARG;
        g_mock.iface_known[ifindex] = true;
        g_mock.iface_up[ifindex] = up;
        return DANOS_OK;
    }
    return nl_iface_up(ifindex, up) == 0 ? DANOS_OK : DANOS_ERR_BACKEND_IO;
}

static danos_status_t ops_route_add(const danos_route_t *route, void *user)
{
    (void)user;
    if (!route || route->prefix.addr.af != DANOS_AF_IPV4)
        return DANOS_ERR_INVALID_ARG;
    if (route->prefix.prefix_len > 32) return DANOS_ERR_INVALID_ARG;

    if (!g_nl.real) {
        if (mock_find(route->prefix.addr.addr, route->prefix.prefix_len,
                      route->vrf_id)) {
            mock_route_t *r = mock_find(route->prefix.addr.addr,
                                        route->prefix.prefix_len,
                                        route->vrf_id);
            (void)r;   /* idempotent re-add */
            return DANOS_OK;
        }
        mock_route_t *r = mock_alloc();
        if (!r) return DANOS_ERR_NO_MEMORY;
        r->used = true;
        r->table_id = route->vrf_id;
        r->len = route->prefix.prefix_len;
        memcpy(r->addr, route->prefix.addr.addr, 4);
        g_mock.route_count++;
        return DANOS_OK;
    }
    return nl_route_msg(route, 1) == 0 ? DANOS_OK : DANOS_ERR_BACKEND_IO;
}

static danos_status_t ops_route_del(const danos_route_t *route, void *user)
{
    (void)user;
    if (!route || route->prefix.addr.af != DANOS_AF_IPV4)
        return DANOS_ERR_INVALID_ARG;
    if (!g_nl.real) {
        mock_route_t *r = mock_find(route->prefix.addr.addr,
                                    route->prefix.prefix_len, route->vrf_id);
        if (!r) return DANOS_ERR_NOT_FOUND;
        r->used = false;
        if (g_mock.route_count) g_mock.route_count--;
        return DANOS_OK;
    }
    return nl_route_msg(route, 0) == 0 ? DANOS_OK : DANOS_ERR_BACKEND_IO;
}

static danos_status_t ops_vrf_add(const danos_vrf_t *vrf, void *user)
{
    (void)user; (void)vrf;
    /* kernel VRF devices are a platform concern; v0.9 accepts and
     * tracks only */
    return DANOS_OK;
}

static danos_status_t ops_vrf_del(danos_vrf_id_t vrf_id, void *user)
{
    (void)user; (void)vrf_id;
    return DANOS_OK;
}

static danos_backend_ops_t g_ops = {
    .name      = "netlink",
    .iface_up  = ops_iface_up,
    .route_add = ops_route_add,
    .route_del = ops_route_del,
    .vrf_add   = ops_vrf_add,
    .vrf_del   = ops_vrf_del,
    .user      = NULL,
};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

int danos_netlink_init(bool real)
{
    memset(&g_mock, 0, sizeof(g_mock));
    g_nl.real = real;
    g_nl.initialized = true;
    if (real) {
        if (nl_open() != 0) {
            /* fall back to mock when rtnetlink is unavailable */
            g_nl.real = false;
            return -1;
        }
    }
    return 0;
}

void danos_netlink_register_backend(void)
{
    danos_backend_ops_set(&g_ops);
}

void danos_netlink_shutdown(void)
{
    if (nl_fd >= 0) {
        close(nl_fd);
        nl_fd = -1;
    }
    g_nl.initialized = false;
}
