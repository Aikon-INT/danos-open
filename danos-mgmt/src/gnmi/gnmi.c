/*
 * DANOS-Open Management: gNMI-like REST/JSON Server Implementation (E2)
 *
 * HTTP/JSON server providing gNMI Get/Set semantics for DPA objects.
 * Uses a minimal HTTP parser (no external dependencies).
 */

#include "gnmi.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* Simple JSON string escape */
static void json_escape(char *out, size_t out_size, const char *in)
{
    size_t i = 0, o = 0;
    while (in[i] && o < out_size - 2) {
        if (in[i] == '"' || in[i] == '\\') {
            if (o < out_size - 2) out[o++] = '\\';
            if (o < out_size - 1) out[o++] = in[i];
        } else {
            out[o++] = in[i];
        }
        i++;
    }
    out[o] = '\0';
}

/* =========================================================================
 * Request handlers (return malloc'd JSON)
 * ========================================================================= */

static char *handle_get_interfaces(void)
{
    /* In production: iterate DPA object store */
    char *resp = malloc(256);
    if (resp) {
        snprintf(resp, 256,
            "{\"path\": \"interfaces\", \"result\": []}");
    }
    return resp;
}

static char *handle_get_routes(void)
{
    char *resp = malloc(256);
    if (resp) {
        snprintf(resp, 256,
            "{\"path\": \"routes\", \"result\": []}");
    }
    return resp;
}

static char *handle_get_vrfs(void)
{
    char *resp = malloc(256);
    if (resp) {
        snprintf(resp, 256,
            "{\"path\": \"vrfs\", \"result\": "
            "[{\"vrf_id\": 0, \"name\": \"default\"}]}");
    }
    return resp;
}

static char *handle_get_capabilities(void)
{
    danos_version_t ver = danos_dpa_get_version();

    char *resp = malloc(256);
    if (resp) {
        snprintf(resp, 256,
            "{\"path\": \"capabilities\", "
            "\"dpa_version\": \"%u.%u.%u\"}",
            ver.major, ver.minor, ver.patch);
    }
    return resp;
}

static char *handle_set_interface(const char *body)
{
    if (!body || body[0] == '\0') {
        return strdup("{\"error\": \"empty body\"}");
    }

    /* Parse minimal JSON: {"ifindex": N, "name": "...", "mtu": N} */
    danos_iface_t iface;
    memset(&iface, 0, sizeof(iface));
    iface.mtu = 1500;
    iface.admin_up = true;

    /* Simple JSON value extraction (not a full parser) */
    const char *p;
    if ((p = strstr(body, "\"ifindex\"")) && (p = strchr(p, ':'))) {
        iface.ifindex = (danos_ifindex_t)atoi(p + 1);
    }
    if ((p = strstr(body, "\"name\""))) {
        /* Find opening quote after colon */
        p = strchr(p, ':');
        if (p) p = strchr(p, '"');
        if (p) {
            p++;  /* skip opening quote */
            size_t i = 0;
            while (*p && *p != '"' && i < sizeof(iface.name) - 1) {
                iface.name[i++] = *p++;
            }
            iface.name[i] = '\0';
        }
    }
    if ((p = strstr(body, "\"mtu\"")) && (p = strchr(p, ':'))) {
        int mtu = atoi(p + 1);
        if (mtu > 0) iface.mtu = (uint16_t)mtu;
    }

    if (iface.ifindex == 0) {
        return strdup("{\"error\": \"ifindex required\"}");
    }

    /* Create via DPA transaction */
    danos_tx_t tx = {0};
    danos_status_t st = danos_tx_begin(&tx, "gnmi-set", NULL);
    if (st != DANOS_OK) {
        return strdup("{\"error\": \"tx_begin failed\"}");
    }

    st = danos_iface_create(&tx, &iface);
    if (st != DANOS_OK) {
        danos_tx_abort(&tx);
        char *resp = malloc(128);
        if (resp) snprintf(resp, 128, "{\"error\": \"create failed: %s\"}",
                          danos_status_str(st));
        return resp;
    }

    st = danos_tx_prepare(&tx);
    if (st == DANOS_OK) st = danos_tx_validate(&tx);
    if (st == DANOS_OK) st = danos_tx_commit(&tx);
    if (st != DANOS_OK) {
        char *resp = malloc(128);
        if (resp) snprintf(resp, 128, "{\"error\": \"commit failed: %s\"}",
                          danos_status_str(st));
        return resp;
    }

    char *resp = malloc(256);
    if (resp) {
        char name_esc[128];
        json_escape(name_esc, sizeof(name_esc), iface.name);
        snprintf(resp, 256,
            "{\"result\": \"created\", \"ifindex\": %u, \"name\": \"%s\", \"mtu\": %u}",
            iface.ifindex, name_esc, iface.mtu);
    }
    return resp;
}

static char *handle_set_route(const char *body)
{
    if (!body || body[0] == '\0') {
        return strdup("{\"error\": \"empty body\"}");
    }

    danos_route_t route;
    memset(&route, 0, sizeof(route));
    route.protocol = DANOS_ROUTE_PROTO_STATIC;
    route.admin_distance = 1;

    const char *p;
    if ((p = strstr(body, "\"vrf_id\"")) && (p = strchr(p, ':'))) {
        route.vrf_id = (danos_vrf_id_t)atoi(p + 1);
    }
    if ((p = strstr(body, "\"prefix_len\"")) && (p = strchr(p, ':'))) {
        route.prefix.prefix_len = (uint8_t)atoi(p + 1);
    }
    if ((p = strstr(body, "\"nhgroup_id\"")) && (p = strchr(p, ':'))) {
        route.nhgroup_id = (danos_obj_id_t)atoi(p + 1);
    }

    if (route.nhgroup_id == 0) {
        return strdup("{\"error\": \"nhgroup_id required\"}");
    }

    danos_tx_t tx = {0};
    danos_status_t st = danos_tx_begin(&tx, "gnmi-set", NULL);
    if (st != DANOS_OK) {
        return strdup("{\"error\": \"tx_begin failed\"}");
    }

    st = danos_route_create(&tx, &route);
    if (st != DANOS_OK) {
        danos_tx_abort(&tx);
        char *resp = malloc(128);
        if (resp) snprintf(resp, 128, "{\"error\": \"create failed: %s\"}",
                          danos_status_str(st));
        return resp;
    }

    st = danos_tx_prepare(&tx);
    if (st == DANOS_OK) st = danos_tx_validate(&tx);
    if (st == DANOS_OK) st = danos_tx_commit(&tx);
    if (st != DANOS_OK) {
        char *resp = malloc(128);
        if (resp) snprintf(resp, 128, "{\"error\": \"commit failed: %s\"}",
                          danos_status_str(st));
        return resp;
    }

    return strdup("{\"result\": \"route created\"}");
}

static char *handle_not_found(const char *path)
{
    char *resp = malloc(1024);
    if (resp) {
        char path_esc[512];
        json_escape(path_esc, sizeof(path_esc), path ? path : "");
        snprintf(resp, 1024, "{\"error\": \"not found\", \"path\": \"%s\"}", path_esc);
    }
    return resp;
}

/* =========================================================================
 * Request dispatch
 * ========================================================================= */

char *danos_gnmi_handle_request(const char *method, const char *path,
                                const char *body)
{
    if (!method || !path) return NULL;

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/gnmi/interfaces") == 0)
            return handle_get_interfaces();
        if (strcmp(path, "/gnmi/routes") == 0)
            return handle_get_routes();
        if (strcmp(path, "/gnmi/vrfs") == 0)
            return handle_get_vrfs();
        if (strcmp(path, "/gnmi/capabilities") == 0)
            return handle_get_capabilities();
        return handle_not_found(path);
    }

    if (strcmp(method, "SET") == 0 || strcmp(method, "POST") == 0) {
        if (strcmp(path, "/gnmi/interfaces") == 0)
            return handle_set_interface(body);
        if (strcmp(path, "/gnmi/routes") == 0)
            return handle_set_route(body);
        return handle_not_found(path);
    }

    return strdup("{\"error\": \"method not allowed\"}");
}

/* =========================================================================
 * HTTP server (minimal)
 * ========================================================================= */

void danos_gnmi_init(danos_gnmi_ctx_t *ctx, uint16_t port)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->listen_fd = -1;
    ctx->port = port;
    ctx->running = false;
}

int danos_gnmi_start(danos_gnmi_ctx_t *ctx)
{
    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(ctx->port);

    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        return -1;
    }

    if (listen(ctx->listen_fd, 8) < 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        return -1;
    }

    ctx->running = true;
    return 0;
}

void danos_gnmi_stop(danos_gnmi_ctx_t *ctx)
{
    if (ctx->listen_fd >= 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
    }
    ctx->running = false;
}

/* =========================================================================
 * gNMI Subscribe (v0.2)
 *
 * Subscription list + per-subscription notification queue. The event-bus
 * callback enqueues a JSON-encoded notification; danos_gnmi_subscribe_poll()
 * drains the queue.
 * ========================================================================= */

typedef struct danos_gnmi_sub_entry {
    danos_gnmi_sub_t      meta;
    danos_gnmi_queue_t    queue;
    struct danos_gnmi_sub_entry *next;
} danos_gnmi_sub_entry_t;

static danos_gnmi_sub_entry_t *g_subs = NULL;
static bool g_subs_inited = false;

/* Forward decl for event callback */
static void gnmi_event_cb(const danos_event_t *event, void *user);

/* Find subscription entry by id */
static danos_gnmi_sub_entry_t *sub_find(uint64_t id)
{
    for (danos_gnmi_sub_entry_t *e = g_subs; e; e = e->next) {
        if (e->meta.id == id) return e;
    }
    return NULL;
}

/* Enqueue a JSON string (takes ownership of `json`) */
static void queue_push(danos_gnmi_queue_t *q, char *json)
{
    if (q->count >= DANOS_GNMI_QUEUE_SIZE) {
        /* Drop oldest */
        free(q->entries[q->head]);
        q->entries[q->head] = NULL;
        q->head = (q->head + 1) % DANOS_GNMI_QUEUE_SIZE;
        q->count--;
        q->dropped++;
    }
    q->entries[q->tail] = json;
    q->tail = (q->tail + 1) % DANOS_GNMI_QUEUE_SIZE;
    q->count++;
}

/* Dequeue one JSON string (caller frees). Returns NULL if empty. */
static char *queue_pop(danos_gnmi_queue_t *q)
{
    if (q->count == 0) return NULL;
    char *json = q->entries[q->head];
    q->entries[q->head] = NULL;
    q->head = (q->head + 1) % DANOS_GNMI_QUEUE_SIZE;
    q->count--;
    return json;
}

/* Map obj_type to JSON name */
static const char *obj_type_name(danos_obj_type_t t)
{
    switch (t) {
        case DANOS_OBJ_IFACE:    return "interface";
        case DANOS_OBJ_VLAN:     return "vlan";
        case DANOS_OBJ_VRF:      return "vrf";
        case DANOS_OBJ_ROUTE:    return "route";
        case DANOS_OBJ_NEXTHOP:  return "nexthop";
        case DANOS_OBJ_NHGROUP:  return "nhgroup";
        case DANOS_OBJ_ACL:      return "acl";
        case DANOS_OBJ_QOS:      return "qos";
        case DANOS_OBJ_MPLS_LSP: return "mpls_lsp";
        case DANOS_OBJ_TUNNEL:   return "tunnel";
        case DANOS_OBJ_EVPN:     return "evpn_evi";
        case DANOS_OBJ_MULTICAST:return "mcast";
        case DANOS_OBJ_BFD:      return "bfd";
        default:                 return "unknown";
    }
}

/* Map event type to JSON name */
static const char *event_type_name(danos_event_type_t t)
{
    switch (t) {
        case DANOS_EVENT_OBJ_CREATED:  return "created";
        case DANOS_EVENT_OBJ_UPDATED:  return "updated";
        case DANOS_EVENT_OBJ_DELETED:  return "deleted";
        case DANOS_EVENT_TX_COMMITTED: return "tx_committed";
        case DANOS_EVENT_TX_ROLLBACK:  return "tx_rollback";
        case DANOS_EVENT_RECONCILE:    return "reconcile";
        case DANOS_EVENT_BACKEND_DOWN: return "backend_down";
        case DANOS_EVENT_BACKEND_UP:   return "backend_up";
        case DANOS_EVENT_CAPABILITY:   return "capability";
        default:                       return "unknown";
    }
}

/* Event bus callback: enqueue JSON notification to matching subscriptions */
static void gnmi_event_cb(const danos_event_t *event, void *user)
{
    /* `user` is the subscription entry pointer */
    danos_gnmi_sub_entry_t *e = (danos_gnmi_sub_entry_t *)user;
    if (!e) return;

    /* Filter by obj_type (INVALID = all) */
    if (e->meta.obj_type != DANOS_OBJ_INVALID &&
        (int)e->meta.obj_type != (int)event->obj_type) {
        return;
    }

    /* Build JSON notification */
    char *json = malloc(256);
    if (!json) return;
    snprintf(json, 256,
        "{\"type\": \"%s\", \"obj_type\": \"%s\", \"obj_id\": %llu, "
        "\"ts_ns\": %llu}",
        event_type_name(event->type),
        obj_type_name(event->obj_type),
        (unsigned long long)event->obj_id,
        (unsigned long long)event->timestamp_ns);
    queue_push(&e->queue, json);
}

void danos_gnmi_subscribe_init(void)
{
    if (g_subs_inited) return;
    g_subs = NULL;
    g_subs_inited = true;
}

void danos_gnmi_subscribe_fini(void)
{
    danos_gnmi_sub_entry_t *e = g_subs;
    while (e) {
        danos_gnmi_sub_entry_t *next = e->next;
        danos_event_unsubscribe(e->meta.id);
        for (size_t i = 0; i < DANOS_GNMI_QUEUE_SIZE; i++) {
            free(e->queue.entries[i]);
        }
        free(e);
        e = next;
    }
    g_subs = NULL;
    g_subs_inited = false;
}

uint64_t danos_gnmi_subscribe(danos_obj_type_t obj_type, uint32_t mask)
{
    if (!g_subs_inited) danos_gnmi_subscribe_init();
    if (mask == 0) return 0;

    danos_gnmi_sub_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return 0;
    e->meta.obj_type = obj_type;
    e->meta.mask = mask;

    /* Register with event bus, passing entry pointer as user data */
    e->meta.id = danos_event_subscribe((danos_event_type_t)mask,
                                       gnmi_event_cb, e);
    if (e->meta.id == 0) {
        free(e);
        return 0;
    }

    /* Link into list */
    e->next = g_subs;
    g_subs = e;
    return e->meta.id;
}

void danos_gnmi_unsubscribe(uint64_t sub_id)
{
    danos_gnmi_sub_entry_t **pp = &g_subs;
    while (*pp) {
        if ((*pp)->meta.id == sub_id) {
            danos_gnmi_sub_entry_t *e = *pp;
            *pp = e->next;
            danos_event_unsubscribe(e->meta.id);
            for (size_t i = 0; i < DANOS_GNMI_QUEUE_SIZE; i++) {
                free(e->queue.entries[i]);
            }
            free(e);
            return;
        }
        pp = &(*pp)->next;
    }
}

char *danos_gnmi_subscribe_poll(uint64_t sub_id)
{
    danos_gnmi_sub_entry_t *e = sub_find(sub_id);
    if (!e) return strdup("{\"error\": \"invalid subscription\"}");

    /* Drain queue into a JSON array */
    size_t buf_size = 256;
    char *buf = malloc(buf_size);
    if (!buf) return NULL;
    size_t pos = 0;
    buf[pos++] = '[';

    bool first = true;
    char *json;
    while ((json = queue_pop(&e->queue)) != NULL) {
        size_t json_len = strlen(json);
        size_t needed = pos + json_len + 4;  /* comma + null */
        if (needed >= buf_size) {
            buf_size = needed * 2;
            char *new_buf = realloc(buf, buf_size);
            if (!new_buf) { free(json); free(buf); return NULL; }
            buf = new_buf;
        }
        if (!first) buf[pos++] = ',';
        memcpy(buf + pos, json, json_len);
        pos += json_len;
        free(json);
        first = false;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

size_t danos_gnmi_subscribe_count(void)
{
    size_t n = 0;
    for (danos_gnmi_sub_entry_t *e = g_subs; e; e = e->next) n++;
    return n;
}
