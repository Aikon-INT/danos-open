/*
 * DANOS-Open Management: gNMI gRPC Server implementation (v0.3)
 *
 * HTTP/2 (h2c) server with HPACK, gRPC message framing
 * ([1B compressed][4B BE length][protobuf]), and gNMI method dispatch.
 */

#include "gnmi_grpc.h"
#include "gnmi_proto.h"
#include "hpack.h"
#include "gnmi.h"          /* existing DPA-backed set helpers reuse */
#include <danos/dpa.h>
#include <danos/core/object_registry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>

#define H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define H2_PREFACE_LEN 24

enum {
    H2_F_DATA = 0, H2_F_HEADERS = 1, H2_F_PRIORITY = 2, H2_F_RST = 3,
    H2_F_SETTINGS = 4, H2_F_PUSH = 5, H2_F_PING = 6, H2_F_GOAWAY = 7,
    H2_F_WINDOW = 8, H2_F_CONT = 9,
};

#define H2_FLAG_END_STREAM  0x1
#define H2_FLAG_ACK         0x1
#define H2_FLAG_END_HEADERS 0x4

#define MAX_FRAME 16384
#define MAX_HEADERS 64
#define MAX_MSG (64 * 1024)

typedef struct {
    int fd;
    hpack_dyn_table_t dyn_enc;   /* encoder-side table (unused, literal only) */
    hpack_dyn_table_t dyn_dec;   /* decoder-side dynamic table */
} h2_conn_t;

/* =========================================================================
 * Frame I/O
 * ========================================================================= */

static int read_full(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t k = recv(fd, p, n, 0);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return -1;
        }
        p += k; n -= (size_t)k;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return -1;
        }
        p += k; n -= (size_t)k;
    }
    return 0;
}

static int write_frame(int fd, uint8_t type, uint8_t flags, uint32_t stream,
                       const void *payload, uint32_t len)
{
    uint8_t hdr[9];
    hdr[0] = (uint8_t)(len >> 16);
    hdr[1] = (uint8_t)(len >> 8);
    hdr[2] = (uint8_t)len;
    hdr[3] = type;
    hdr[4] = flags;
    hdr[5] = (uint8_t)(stream >> 24);
    hdr[6] = (uint8_t)(stream >> 16);
    hdr[7] = (uint8_t)(stream >> 8);
    hdr[8] = (uint8_t)stream;
    if (write_full(fd, hdr, 9) < 0) return -1;
    if (len && write_full(fd, payload, len) < 0) return -1;
    return 0;
}

/* =========================================================================
 * gRPC response helpers
 * ========================================================================= */

static int send_grpc_response(int fd, uint32_t stream,
                              const uint8_t *msg, size_t len)
{
    /* HEADERS: :status 200 + content-type application/grpc */
    uint8_t hbuf[128];
    size_t hlen = 0;
    int k = hpack_encode_literal(hbuf + hlen, sizeof(hbuf) - hlen,
                                 ":status", "200");
    if (k < 0) return -1;
    hlen += (size_t)k;
    k = hpack_encode_literal(hbuf + hlen, sizeof(hbuf) - hlen,
                             "content-type", "application/grpc");
    if (k < 0) return -1;
    hlen += (size_t)k;

    if (write_frame(fd, H2_F_HEADERS, 0x4 /* END_HEADERS */, stream,
                    hbuf, (uint32_t)hlen) < 0)
        return -1;

    /* DATA: gRPC frame */
    uint8_t gbuf[5];
    gbuf[0] = 0;  /* not compressed */
    gbuf[1] = (uint8_t)(len >> 24);
    gbuf[2] = (uint8_t)(len >> 16);
    gbuf[3] = (uint8_t)(len >> 8);
    gbuf[4] = (uint8_t)len;
    if (write_frame(fd, H2_F_DATA, 0, stream, gbuf, 5) < 0) return -1;
    if (len && write_frame(fd, H2_F_DATA, 0, stream, msg, (uint32_t)len) < 0)
        return -1;
    /* END_STREAM on the last DATA piece */
    if (len == 0) {
        if (write_frame(fd, H2_F_DATA, H2_FLAG_END_STREAM, stream, NULL, 0) < 0)
            return -1;
    }

    /* Trailers: grpc-status 0, END_STREAM */
    uint8_t tbuf[64];
    size_t tlen = 0;
    k = hpack_encode_literal(tbuf, sizeof(tbuf), "grpc-status", "0");
    if (k < 0) return -1;
    tlen = (size_t)k;
    return write_frame(fd, H2_F_HEADERS, H2_FLAG_END_STREAM | H2_FLAG_END_HEADERS,
                       stream, tbuf, (uint32_t)tlen);
}

/* =========================================================================
 * gNMI handlers (protobuf level)
 * ========================================================================= */

static danos_state_store_t *g_store;

static danos_state_store_t *store_or_default(void)
{
    return g_store ? g_store : NULL;
}

/* Serialize one object as a JSON-ish value (we store DPA structs; emit
 * a compact JSON encoding per object type). */
static int obj_to_json(danos_obj_type_t type, const void *data, size_t size,
                       char *out, size_t cap)
{
    if (type == DANOS_OBJ_IFACE && size >= sizeof(danos_iface_t)) {
        const danos_iface_t *i = data;
        snprintf(out, cap,
                 "{\"ifindex\":%u,\"name\":\"%s\",\"mtu\":%u,\"admin_up\":%s}",
                 i->ifindex, i->name, i->mtu, i->admin_up ? "true" : "false");
        return 0;
    }
    if (type == DANOS_OBJ_ROUTE && size >= sizeof(danos_route_t)) {
        const danos_route_t *r = data;
        char pfx[64];
        if (r->prefix.addr.af == DANOS_AF_IPV4) {
            snprintf(pfx, sizeof(pfx), "%u.%u.%u.%u/%u",
                     r->prefix.addr.addr[0], r->prefix.addr.addr[1],
                     r->prefix.addr.addr[2], r->prefix.addr.addr[3],
                     r->prefix.prefix_len);
        } else {
            snprintf(pfx, sizeof(pfx), "<ipv6>/%u", r->prefix.prefix_len);
        }
        snprintf(out, cap, "{\"prefix\":\"%s\",\"vrf\":%u}",
                 pfx, r->vrf_id);
        return 0;
    }
    if (type == DANOS_OBJ_VRF && size >= sizeof(danos_vrf_t)) {
        const danos_vrf_t *v = data;
        snprintf(out, cap, "{\"vrf_id\":%u,\"name\":\"%s\"}",
                 v->vrf_id, v->name);
        return 0;
    }
    snprintf(out, cap, "{}");
    return -1;
}

/* Collect objects of one type (iterate desired store buckets) */
typedef struct {
    danos_obj_type_t type;
    void *out;
    size_t out_size;     /* per-element size */
    size_t max;
    size_t count;
} collect_ctx_t;

static void collect_cb(danos_object_entry_t *e, void *user)
{
    collect_ctx_t *c = user;
    if (e->type != c->type || c->count >= c->max) return;
    char *dst = (char *)c->out + c->count * c->out_size;
    size_t n = e->data_size < c->out_size ? e->data_size : c->out_size;
    memcpy(dst, e->data, n);
    c->count++;
}

static uint32_t collect_objects(danos_state_store_t *ss, danos_obj_type_t type,
                                void *out, size_t elem_size, size_t max)
{
    /* No explicit store: read the DPA default store (the same store
     * danos_tx_commit writes to). */
    danos_object_store_t *src = (ss && ss->desired) ? ss->desired : g_default_store;
    if (!src) return 0;
    collect_ctx_t c = { .type = type, .out = out, .out_size = elem_size,
                        .max = max, .count = 0 };
    danos_object_iterate(src, collect_cb, &c);
    return (uint32_t)c.count;
}

int gnmi_handle_capabilities(const uint8_t *req, size_t req_len,
                             uint8_t *resp, size_t resp_cap)
{
    (void)req; (void)req_len;   /* CapabilityRequest has only extensions */
    gnmi_pb_t w;
    gnmi_pb_init(&w, resp, resp_cap);

    danos_version_t ver = danos_dpa_get_version();
    char vstr[48];
    snprintf(vstr, sizeof(vstr), "DANOS-Open DPA %u.%u.%u",
             ver.major, ver.minor, ver.patch);

    gnmi_model_data_t models[] = {
        { "openconfig-interfaces", "OpenConfig", "2.4.1" },
        { "openconfig-network-instance", "OpenConfig", "0.16.2" },
        { "danos-dpa", "DANOS-Open", "0.3" },
    };
    gnmi_encoding_t encs[] = { GNMI_ENC_JSON, GNMI_ENC_JSON_IETF };
    gnmi_encode_capabilities_response(&w, models, 3, encs, 2, vstr);
    return w.overflow ? -1 : (int)w.len;
}

int gnmi_handle_get(danos_state_store_t *store,
                    const uint8_t *req, size_t req_len,
                    uint8_t *resp, size_t resp_cap)
{
    gnmi_get_request_t gr;
    if (!gnmi_decode_get_request(req, req_len, &gr)) return -1;

    danos_state_store_t *ss = store ? store : store_or_default();

    gnmi_pb_t w;
    gnmi_pb_init(&w, resp, resp_cap);
    size_t saved = gnmi_pb_begin_nested(&w, 1 /* notification */);

    /* timestamp = 1 */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    gnmi_pb_put_uint64(&w, 1, (uint64_t)ts.tv_sec * 1000000000ULL +
                                  (uint64_t)ts.tv_nsec);

    for (uint32_t i = 0; i < gr.path_count; i++) {
        const gnmi_path_t *p = &gr.paths[i];
        /* expected shapes: interfaces/interface[name=X], interfaces,
         * routes, vrfs, vrf instances */
        if (p->elem_count == 0) continue;
        const char *top = p->elems[0].name;

        if (strcmp(top, "interfaces") == 0) {
            danos_iface_t ifaces[16];
            uint32_t n = collect_objects(ss, DANOS_OBJ_IFACE, ifaces,
                                         sizeof(ifaces[0]), 16);
            for (uint32_t j = 0; j < n; j++) {
                if (p->elem_count >= 2 && p->elems[1].has_key &&
                    strcmp(p->elems[1].key_value, ifaces[j].name) != 0)
                    continue;
                char json[256];
                obj_to_json(DANOS_OBJ_IFACE, &ifaces[j], sizeof(ifaces[j]),
                            json, sizeof(json));
                /* Update { path=1, val=3 } */
                size_t us = gnmi_pb_begin_nested(&w, 4);
                gnmi_encode_path(&w, 1, p);
                gnmi_typed_value_t v = { .kind = GNMI_VAL_JSON_IETF };
                snprintf(v.s, sizeof(v.s), "%s", json);
                gnmi_encode_typed_value(&w, 3, &v);
                gnmi_pb_end_nested(&w, us);
            }
        } else if (strcmp(top, "vrfs") == 0) {
            danos_vrf_t vrfs[16];
            uint32_t n = collect_objects(ss, DANOS_OBJ_VRF, vrfs,
                                         sizeof(vrfs[0]), 16);
            for (uint32_t j = 0; j < n; j++) {
                char json[256];
                obj_to_json(DANOS_OBJ_VRF, &vrfs[j], sizeof(vrfs[j]),
                            json, sizeof(json));
                size_t us = gnmi_pb_begin_nested(&w, 4);
                gnmi_encode_path(&w, 1, p);
                gnmi_typed_value_t v = { .kind = GNMI_VAL_JSON_IETF };
                snprintf(v.s, sizeof(v.s), "%s", json);
                gnmi_encode_typed_value(&w, 3, &v);
                gnmi_pb_end_nested(&w, us);
            }
        } else if (strcmp(top, "routes") == 0) {
            danos_route_t routes[32];
            uint32_t n = collect_objects(ss, DANOS_OBJ_ROUTE, routes,
                                         sizeof(routes[0]), 32);
            for (uint32_t j = 0; j < n; j++) {
                char json[256];
                obj_to_json(DANOS_OBJ_ROUTE, &routes[j], sizeof(routes[j]),
                            json, sizeof(json));
                size_t us = gnmi_pb_begin_nested(&w, 4);
                gnmi_encode_path(&w, 1, p);
                gnmi_typed_value_t v = { .kind = GNMI_VAL_JSON_IETF };
                snprintf(v.s, sizeof(v.s), "%s", json);
                gnmi_encode_typed_value(&w, 3, &v);
                gnmi_pb_end_nested(&w, us);
            }
        }
    }
    gnmi_pb_end_nested(&w, saved);
    return w.overflow ? -1 : (int)w.len;
}

/* Extract an unsigned field from a flat JSON body */
static const char *json_find(const char *body, const char *key)
{
    const char *p = strstr(body, key);
    if (!p) return NULL;
    p = strchr(p, ':');
    return p ? p + 1 : NULL;
}

static int json_get_uint(const char *body, const char *key, unsigned *out)
{
    const char *p = json_find(body, key);
    if (!p) return -1;
    while (*p == ' ') p++;
    *out = (unsigned)strtoul(p, NULL, 10);
    return 0;
}

int gnmi_handle_set(danos_state_store_t *store,
                    const uint8_t *req, size_t req_len,
                    uint8_t *resp, size_t resp_cap)
{
    gnmi_set_request_t sr;
    if (!gnmi_decode_set_request(req, req_len, &sr)) return -1;

    danos_state_store_t *ss = store ? store : store_or_default();

    gnmi_set_response_t out;
    memset(&out, 0, sizeof(out));
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    out.timestamp = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    danos_status_t status = DANOS_OK;

    /* update: interfaces/interface[name=X] val=json {"mtu":N,...} */
    for (uint32_t i = 0; i < sr.update_count && status == DANOS_OK; i++) {
        const gnmi_update_t *u = &sr.updates[i];
        if (u->path.elem_count < 2 ||
            strcmp(u->path.elems[0].name, "interfaces") != 0 ||
            strcmp(u->path.elems[1].name, "interface") != 0 ||
            !u->path.elems[1].has_key) {
            status = DANOS_ERR_INVALID_ARG;
            break;
        }
        const char *ifname = u->path.elems[1].key_value;
        danos_iface_t iface;
        memset(&iface, 0, sizeof(iface));
        snprintf(iface.name, sizeof(iface.name), "%s", ifname);
        unsigned idx = (unsigned)strtoul(ifname, NULL, 10);
        if (idx == 0 && ifname[0] != '0') {
            /* non-numeric name: allocate max(ifindex)+1 */
            danos_iface_t all[64];
            uint32_t cnt = collect_objects(ss, DANOS_OBJ_IFACE, all,
                                           sizeof(all[0]), 64);
            for (uint32_t k = 0; k < cnt; k++) {
                if (all[k].ifindex >= idx) idx = all[k].ifindex;
            }
            idx++;
        }
        iface.ifindex = (danos_ifindex_t)idx;
        iface.mtu = 1500;
        iface.admin_up = true;
        if (u->val.kind == GNMI_VAL_JSON || u->val.kind == GNMI_VAL_JSON_IETF) {
            unsigned mtu;
            if (json_get_uint(u->val.s, "mtu", &mtu) == 0)
                iface.mtu = (uint16_t)mtu;
            unsigned up = 1;
            if (json_get_uint(u->val.s, "admin_up", &up) == 0)
                iface.admin_up = up != 0;
        }

        danos_tx_t tx;
        if (danos_tx_begin(&tx, "gnmi-grpc", NULL) != DANOS_OK) {
            status = DANOS_ERR_BACKEND_IO;
            break;
        }
        danos_status_t st = danos_iface_create(&tx, &iface);
        if (st == DANOS_OK) st = danos_tx_prepare(&tx);
        if (st == DANOS_OK) st = danos_tx_validate(&tx);
        if (st == DANOS_OK) st = danos_tx_commit(&tx);
        if (st != DANOS_OK) {
            danos_tx_abort(&tx);
            status = st;
            break;
        }
        out.result_paths[out.result_count++] = u->path;
        out.ops[out.result_count - 1] = GNMI_OP_UPDATE;
    }

    /* delete: interfaces/interface[name=X] */
    for (uint32_t i = 0; i < sr.delete_count && status == DANOS_OK; i++) {
        const gnmi_path_t *p = &sr.deletes[i];
        if (p->elem_count < 2 ||
            strcmp(p->elems[0].name, "interfaces") != 0 ||
            !p->elems[1].has_key) {
            status = DANOS_ERR_INVALID_ARG;
            break;
        }
        {
            /* find by name in the desired/default store */
            danos_iface_t ifaces[16];
            uint32_t n = collect_objects(ss, DANOS_OBJ_IFACE, ifaces,
                                         sizeof(ifaces[0]), 16);
            bool found = false;
            for (uint32_t j = 0; j < n; j++) {
                if (strcmp(ifaces[j].name, p->elems[1].key_value) == 0) {
                    danos_object_delete(ss->desired, DANOS_OBJ_IFACE,
                                        ifaces[j].ifindex);
                    found = true;
                    break;
                }
            }
            if (!found) status = DANOS_ERR_NOT_FOUND;
        }
        if (status == DANOS_OK) {
            out.result_paths[out.result_count++] = *p;
            out.ops[out.result_count - 1] = GNMI_OP_DELETE;
        }
    }

    if (status != DANOS_OK) return -(int)status;

    /* encode into resp buffer */
    gnmi_pb_t w;
    gnmi_pb_init(&w, resp, resp_cap);
    if (!gnmi_encode_set_response(&w, &out)) return -1;
    return (int)w.len;
}

/* =========================================================================
 * HTTP/2 connection handling
 * ========================================================================= */

typedef struct {
    char *path;
    char *method;
    char *content_type;
    char *te;
} req_headers_t;

static bool header_cb(const char *name, const char *value, void *user)
{
    req_headers_t *h = user;
    if (strcmp(name, ":path") == 0 && !h->path) h->path = strdup(value);
    else if (strcmp(name, ":method") == 0 && !h->method) h->method = strdup(value);
    else if (strcmp(name, "content-type") == 0 && !h->content_type)
        h->content_type = strdup(value);
    else if (strcmp(name, "te") == 0 && !h->te) h->te = strdup(value);
    return true;
}

static void headers_free(req_headers_t *h)
{
    free(h->path); free(h->method); free(h->content_type); free(h->te);
    memset(h, 0, sizeof(*h));
}

/* Read a gRPC length-prefixed message from DATA frames of a stream.
 * Returns message length (>=0, may be 0 for an empty message),
 * -2 if the stream ended before any gRPC frame arrived,
 * -1 on error. */
#define GRPC_READ_NO_MSG (-2)
static int read_grpc_message(h2_conn_t *c, uint32_t stream,
                             uint8_t *msg, size_t cap)
{
    uint8_t frame[MAX_FRAME];
    size_t have = 0;      /* bytes of gRPC frame header collected */
    uint8_t ghdr[5];
    bool hdr_done = false;
    size_t need = 0;
    size_t got = 0;

    for (;;) {
        uint8_t fh[9];
        if (read_full(c->fd, fh, 9) < 0) return -1;
        uint32_t flen = ((uint32_t)fh[0] << 16) | ((uint32_t)fh[1] << 8) | fh[2];
        uint8_t ftype = fh[3], fflags = fh[4];
        uint32_t fstream = ((uint32_t)fh[5] << 24) | ((uint32_t)fh[6] << 16) |
                           ((uint32_t)fh[7] << 8) | fh[8];

        if (flen > MAX_FRAME) return -1;
        if (flen && read_full(c->fd, frame, flen) < 0) return -1;

        if (ftype == H2_F_SETTINGS) {
            if (!(fflags & H2_FLAG_ACK))
                write_frame(c->fd, H2_F_SETTINGS, H2_FLAG_ACK, 0, NULL, 0);
            continue;
        }
        if (ftype == H2_F_PING) {
            if (!(fflags & H2_FLAG_ACK))
                write_frame(c->fd, H2_F_PING, H2_FLAG_ACK, 0, frame, flen);
            continue;
        }
        if (ftype == H2_F_GOAWAY || ftype == H2_F_RST) return -1;
        if (ftype == H2_F_WINDOW || ftype == H2_F_PRIORITY) continue;

        if (ftype == H2_F_HEADERS) {
            if (fflags & H2_FLAG_END_STREAM)
                return hdr_done ? (int)got : GRPC_READ_NO_MSG;
            continue;
        }

        if (ftype != H2_F_DATA || fstream != stream) continue;

        const uint8_t *p = frame;
        uint32_t n = flen;
        while (n > 0 || (hdr_done && got == need)) {
            if (!hdr_done) {
                if (n == 0) break;
                ghdr[have++] = *p++;
                n--;
                if (have < 5) continue;
                if (ghdr[0] != 0) return -1;  /* compressed flag */
                need = ((size_t)ghdr[1] << 24) | ((size_t)ghdr[2] << 16) |
                       ((size_t)ghdr[3] << 8) | ghdr[4];
                if (need > cap) return -1;
                hdr_done = true;
                got = 0;
                if (need == 0) return 0;   /* empty message */
                continue;
            }
            /* hdr_done && need > 0 */
            if (got == need) return (int)got;
            if (n == 0) break;
            size_t take = need - got;
            if (take > n) take = n;
            memcpy(msg + got, p, take);
            got += take;
            p += take;
            n -= take;
        }
        if (fflags & H2_FLAG_END_STREAM)
            return hdr_done ? (int)got : GRPC_READ_NO_MSG;
    }
}

int danos_gnmi_grpc_serve_fd(int fd)
{
    h2_conn_t c;
    memset(&c, 0, sizeof(c));
    c.fd = fd;
    hpack_dyn_init(&c.dyn_dec);

    /* 1. client preface */
    char preface[H2_PREFACE_LEN];
    if (read_full(fd, preface, H2_PREFACE_LEN) < 0 ||
        memcmp(preface, H2_PREFACE, H2_PREFACE_LEN) != 0) {
        hpack_dyn_free(&c.dyn_dec);
        return -1;
    }

    /* 2. our SETTINGS */
    write_frame(fd, H2_F_SETTINGS, 0, 0, NULL, 0);

    int rc = 0;
    uint8_t msg[MAX_MSG];

    for (;;) {
        uint8_t fh[9];
        if (read_full(fd, fh, 9) < 0) break;
        uint32_t flen = ((uint32_t)fh[0] << 16) | ((uint32_t)fh[1] << 8) | fh[2];
        uint8_t ftype = fh[3], fflags = fh[4];
        uint32_t fstream = ((uint32_t)fh[5] << 24) | ((uint32_t)fh[6] << 16) |
                           ((uint32_t)fh[7] << 8) | fh[8];
        if (flen > MAX_FRAME) { rc = -1; break; }

        uint8_t frame[MAX_FRAME];
        if (flen && read_full(fd, frame, flen) < 0) { rc = -1; break; }

        if (ftype == H2_F_SETTINGS) {
            if (!(fflags & H2_FLAG_ACK))
                write_frame(fd, H2_F_SETTINGS, H2_FLAG_ACK, 0, NULL, 0);
            continue;
        }
        if (ftype == H2_F_PING) {
            if (!(fflags & H2_FLAG_ACK))
                write_frame(fd, H2_F_PING, H2_FLAG_ACK, 0, frame, flen);
            continue;
        }
        if (ftype == H2_F_GOAWAY) break;
        if (ftype == H2_F_WINDOW || ftype == H2_F_PRIORITY) continue;
        if (ftype == H2_F_RST) continue;

        if (ftype != H2_F_HEADERS) continue;
        if (!(fflags & H2_FLAG_END_HEADERS)) {
            /* CONTINUATION frames follow; fold them in */
            for (;;) {
                uint8_t ch[9];
                if (read_full(fd, ch, 9) < 0) { rc = -1; goto out; }
                uint32_t clen = ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | ch[2];
                uint8_t cflags = ch[4];
                uint8_t cframe[MAX_FRAME];
                if (clen && read_full(fd, cframe, clen) < 0) { rc = -1; goto out; }
                if (flen + clen <= sizeof(frame)) {
                    memcpy(frame + flen, cframe, clen);
                    flen += clen;
                }
                if (cflags & H2_FLAG_END_HEADERS) break;
            }
        }

        req_headers_t h;
        memset(&h, 0, sizeof(h));
        if (!hpack_decode(&c.dyn_dec, frame, flen, header_cb, &h)) {
            headers_free(&h);
            rc = -1;
            break;
        }

        bool end_stream = (fflags & H2_FLAG_END_STREAM) != 0;
        size_t msg_len = 0;
        if (!end_stream) {
            /* read the request message (handles its own frames) */
            int n = read_grpc_message(&c, fstream, msg, sizeof(msg));
            if (n == -1) { headers_free(&h); rc = -1; break; }
            if (n == GRPC_READ_NO_MSG) { headers_free(&h); continue; }
            msg_len = (size_t)n;
        }

        /* dispatch on :path */
        uint8_t resp[MAX_MSG];
        int rlen = -1;
        if (h.path && strcmp(h.path, "/gnmi.gNMI/Capabilities") == 0) {
            rlen = gnmi_handle_capabilities(msg, msg_len, resp, sizeof(resp));
        } else if (h.path && strcmp(h.path, "/gnmi.gNMI/Get") == 0) {
            rlen = end_stream ? -1
                              : gnmi_handle_get(store_or_default(), msg,
                                                msg_len, resp, sizeof(resp));
        } else if (h.path && strcmp(h.path, "/gnmi.gNMI/Set") == 0) {
            rlen = end_stream ? -1
                              : gnmi_handle_set(store_or_default(), msg,
                                                msg_len, resp, sizeof(resp));
        } else {
            /* unknown method: grpc-status 12 (unimplemented) */
            headers_free(&h);
            uint8_t tbuf[32];
            int k = hpack_encode_literal(tbuf, sizeof(tbuf),
                                         "grpc-status", "12");
            write_frame(fd, H2_F_HEADERS,
                        H2_FLAG_END_STREAM | H2_FLAG_END_HEADERS,
                        fstream, tbuf, (size_t)k);
            continue;
        }

        if (rlen >= 0) {
            send_grpc_response(fd, fstream, resp, (size_t)rlen);
        } else {
            uint8_t tbuf[48];
            int k = hpack_encode_literal(tbuf, sizeof(tbuf),
                                         "grpc-status", "2");
            write_frame(fd, H2_F_HEADERS,
                        H2_FLAG_END_STREAM | H2_FLAG_END_HEADERS,
                        fstream, tbuf, (size_t)k);
        }
        headers_free(&h);
    }
out:
    hpack_dyn_free(&c.dyn_dec);
    return rc;
}

/* =========================================================================
 * Server lifecycle
 * ========================================================================= */

static struct {
    danos_gnmi_grpc_ctx_t *ctx;
    pthread_t thread;
} g_grpc;

typedef struct {
    int fd;
    danos_gnmi_grpc_ctx_t *ctx;
} grpc_conn_arg_t;

static void *grpc_conn_thread(void *argp)
{
    grpc_conn_arg_t *a = argp;
    danos_gnmi_grpc_serve_fd(a->fd);
    close(a->fd);
    a->ctx->rpcs_served++;
    free(a);
    return NULL;
}

static void *grpc_accept_loop(void *arg)
{
    danos_gnmi_grpc_ctx_t *ctx = arg;
    while (ctx->running) {
        int fd = accept(ctx->listen_fd, NULL, NULL);
        if (fd < 0) break;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        grpc_conn_arg_t *a = malloc(sizeof(*a));
        if (!a) {
            close(fd);
            continue;
        }
        a->fd = fd;
        a->ctx = ctx;
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, grpc_conn_thread, a) != 0) {
            close(fd);
            free(a);
        }
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

void danos_gnmi_grpc_init(danos_gnmi_grpc_ctx_t *ctx, uint16_t port)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->listen_fd = -1;
    ctx->port = port;
}

void danos_gnmi_grpc_set_store(danos_gnmi_grpc_ctx_t *ctx,
                               danos_state_store_t *store)
{
    ctx->store = store;
    g_store = store;
}

int danos_gnmi_grpc_start(danos_gnmi_grpc_ctx_t *ctx)
{
    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) return -1;
    int one = 1;
    setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(ctx->port);
    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(ctx->listen_fd, 4) < 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        return -1;
    }
    ctx->running = true;
    g_grpc.ctx = ctx;
    if (pthread_create(&g_grpc.thread, NULL, grpc_accept_loop, ctx) != 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        ctx->running = false;
        return -1;
    }
    return 0;
}

void danos_gnmi_grpc_stop(danos_gnmi_grpc_ctx_t *ctx)
{
    if (!ctx->running) return;
    ctx->running = false;
    shutdown(ctx->listen_fd, SHUT_RDWR);
    close(ctx->listen_fd);
    ctx->listen_fd = -1;
    pthread_join(g_grpc.thread, NULL);
}
