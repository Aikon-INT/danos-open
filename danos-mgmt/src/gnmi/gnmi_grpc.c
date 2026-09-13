/*
 * DANOS-Open Management: gNMI gRPC Server implementation (v0.3)
 *
 * HTTP/2 (h2c) server with HPACK, gRPC message framing
 * ([1B compressed][4B BE length][protobuf]), and gNMI method dispatch.
 */

#include "gnmi_grpc.h"
#include "gnmi_proto.h"
#include "model_paths.h"
#include "model_routes.h"
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
#define MAX_MSG (256 * 1024)

/* Frames read out of order (e.g. another stream's DATA while we are
 * window-blocked on a response) are parked here. */
typedef struct pending_frame {
    uint8_t type, flags;
    uint32_t stream;
    uint8_t *payload;
    uint32_t len;
    struct pending_frame *next;
} pending_frame_t;

typedef struct {
    int fd;
    hpack_dyn_table_t dyn_dec;   /* decoder-side dynamic table */
    /* HTTP/2 flow control (our send side) */
    int64_t conn_window;         /* stream-0 budget */
    int64_t stream_window[64];   /* indexed by (stream % 64) */
    uint32_t peer_initial_window;
    pending_frame_t *pending;
} h2_conn_t;

#define H2_DEFAULT_WINDOW 65535
#define H2_MAX_FRAME_LEN  16384

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
 * Frame receive with pending queue + flow-control state machine
 * ========================================================================= */

static void pending_push(h2_conn_t *c, uint8_t type, uint8_t flags,
                         uint32_t stream, const uint8_t *payload, uint32_t len)
{
    pending_frame_t *pf = malloc(sizeof(*pf));
    if (!pf) return;
    pf->type = type;
    pf->flags = flags;
    pf->stream = stream;
    pf->len = len;
    pf->payload = len ? malloc(len) : NULL;
    if (len && !pf->payload) {
        free(pf);
        return;
    }
    if (len) memcpy(pf->payload, payload, len);
    pf->next = c->pending;
    c->pending = pf;
}

static bool pending_pop(h2_conn_t *c, uint8_t *type, uint8_t *flags,
                        uint32_t *stream, uint8_t *payload, uint32_t *len)
{
    pending_frame_t *pf = c->pending;
    if (!pf) return false;
    c->pending = pf->next;
    *type = pf->type;
    *flags = pf->flags;
    *stream = pf->stream;
    *len = pf->len < *len ? pf->len : *len;
    if (pf->len && payload) memcpy(payload, pf->payload, *len);
    free(pf->payload);
    free(pf);
    return true;
}

static void pending_free_all(h2_conn_t *c)
{
    while (c->pending) {
        pending_frame_t *pf = c->pending;
        c->pending = pf->next;
        free(pf->payload);
        free(pf);
    }
}

/* Read one frame from the wire, handling connection-level housekeeping
 * (SETTINGS ack + window updates, PING ack). Frames for other streams
 * encountered while the caller waits are returned to it via the queue;
 * this function only returns frames that matter to the caller. */
/* Returns 0 = frame delivered, 1 = housekeeping processed (flow-control
 * windows may have changed; caller should re-check budget), -1 = error. */
static int recv_frame(h2_conn_t *c, uint8_t *type, uint8_t *flags,
                      uint32_t *stream, uint8_t *payload, uint32_t *len)
{
    for (;;) {
        if (pending_pop(c, type, flags, stream, payload, len)) return 0;

        uint8_t fh[9];
        if (read_full(c->fd, fh, 9) < 0) return -1;
        uint32_t flen = ((uint32_t)fh[0] << 16) | ((uint32_t)fh[1] << 8) | fh[2];
        uint8_t ftype = fh[3], fflags = fh[4];
        uint32_t fstream = ((uint32_t)fh[5] << 24) | ((uint32_t)fh[6] << 16) |
                           ((uint32_t)fh[7] << 8) | fh[8];
        if (flen > H2_MAX_FRAME_LEN) return -1;
        uint8_t fbuf[H2_MAX_FRAME_LEN];
        if (flen && read_full(c->fd, fbuf, flen) < 0) return -1;

        switch (ftype) {
        case H2_F_SETTINGS: {
            if (!(fflags & H2_FLAG_ACK)) {
                /* parse peer INITIAL_WINDOW_SIZE (id 4) and MAX_FRAME_SIZE (5) */
                if (flen % 6 == 0) {
                    for (uint32_t i = 0; i + 6 <= flen; i += 6) {
                        uint16_t id = (uint16_t)((fbuf[i] << 8) | fbuf[i + 1]);
                        uint32_t v = ((uint32_t)fbuf[i + 2] << 24) |
                                     ((uint32_t)fbuf[i + 3] << 16) |
                                     ((uint32_t)fbuf[i + 4] << 8) | fbuf[i + 5];
                        if (id == 0x4) {
                            c->peer_initial_window = v;
                            for (int w = 0; w < 64; w++)
                                c->stream_window[w] = v;
                        }
                    }
                }
                write_frame(c->fd, H2_F_SETTINGS, H2_FLAG_ACK, 0, NULL, 0);
            }
            continue;   /* housekeeping: caller never sees SETTINGS */
        }
        case H2_F_PING:
            if (!(fflags & H2_FLAG_ACK))
                write_frame(c->fd, H2_F_PING, H2_FLAG_ACK, 0, fbuf, flen);
            continue;
        case H2_F_WINDOW: {
            if (flen < 4) continue;
            uint32_t inc = ((uint32_t)fbuf[0] << 24) | ((uint32_t)fbuf[1] << 16) |
                           ((uint32_t)fbuf[2] << 8) | fbuf[3];
            if (fstream == 0) {
                c->conn_window += inc;
            } else {
                c->stream_window[fstream % 64] += inc;
            }
            return 1;   /* caller may be unblocked */
        }
        default:
            *type = ftype;
            *flags = fflags;
            *stream = fstream;
            *len = flen;
            if (flen && payload) memcpy(payload, fbuf, flen);
            return 0;
        }
    }
}

/* Send `len` bytes as DATA frames respecting peer flow-control windows.
 * While blocked on window budget, incoming frames are queued. */
static int send_data_windowed(h2_conn_t *c, uint32_t stream,
                              const uint8_t *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int64_t budget = c->stream_window[stream % 64];
        if (budget > c->conn_window) budget = c->conn_window;
        if (budget > H2_MAX_FRAME_LEN) budget = H2_MAX_FRAME_LEN;

        if (budget <= 0) {
            /* wait for WINDOW_UPDATE; park unrelated frames */
            uint8_t t, fl;
            uint32_t st;
            uint32_t plen = H2_MAX_FRAME_LEN;
            uint8_t pl[H2_MAX_FRAME_LEN];
            int rr = recv_frame(c, &t, &fl, &st, pl, &plen);
            if (rr < 0) return -1;
            if (rr == 1) continue;              /* window refilled */
            if ((t == H2_F_DATA || t == H2_F_HEADERS) && st != stream)
                pending_push(c, t, fl, st, pl, plen);
            continue;
        }

        size_t chunk = (size_t)budget < len - sent ? (size_t)budget : len - sent;
        if (write_frame(c->fd, H2_F_DATA, 0, stream,
                        data + sent, (uint32_t)chunk) < 0) return -1;
        sent += chunk;
        c->stream_window[stream % 64] -= (int64_t)chunk;
        c->conn_window -= (int64_t)chunk;
    }
    return 0;
}

/* =========================================================================
 * gRPC response helpers
 * ========================================================================= */

static int send_grpc_response(h2_conn_t *c, uint32_t stream,
                              const uint8_t *msg, size_t len)
{
    int fd = c->fd;
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

    /* DATA: gRPC frame header + windowed payload */
    uint8_t gbuf[5];
    gbuf[0] = 0;  /* not compressed */
    gbuf[1] = (uint8_t)(len >> 24);
    gbuf[2] = (uint8_t)(len >> 16);
    gbuf[3] = (uint8_t)(len >> 8);
    gbuf[4] = (uint8_t)len;
    if (write_frame(fd, H2_F_DATA, 0, stream, gbuf, 5) < 0) return -1;
    if (send_data_windowed(c, stream, msg, len) < 0) return -1;
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

/* Send response HEADERS for a streaming RPC (no END_STREAM) */
static int send_stream_headers(h2_conn_t *c, uint32_t stream)
{
    int fd = c->fd;
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
    return write_frame(fd, H2_F_HEADERS, 0x4, stream, hbuf, (uint32_t)hlen);
}

/* Send one gRPC message on an open stream (DATA frames only) */
static int send_stream_message(h2_conn_t *c, uint32_t stream,
                               const uint8_t *msg, size_t len)
{
    int fd = c->fd;
    uint8_t gbuf[5] = { 0, (uint8_t)(len >> 24), (uint8_t)(len >> 16),
                        (uint8_t)(len >> 8), (uint8_t)len };
    if (write_frame(fd, H2_F_DATA, 0, stream, gbuf, 5) < 0) return -1;
    return send_data_windowed(c, stream, msg, len);
}

static int send_stream_trailers(h2_conn_t *c, uint32_t stream)
{
    int fd = c->fd;
    uint8_t tbuf[64];
    int k = hpack_encode_literal(tbuf, sizeof(tbuf), "grpc-status", "0");
    if (k < 0) return -1;
    return write_frame(fd, H2_F_HEADERS,
                       H2_FLAG_END_STREAM | H2_FLAG_END_HEADERS,
                       stream, tbuf, (size_t)k);
}

/* Build a notification containing every object matching one path */
static int build_notification_for_paths(const gnmi_subscribe_list_t *sl,
                                        const gnmi_path_t *paths,
                                        uint32_t path_count,
                                        uint8_t *resp, size_t resp_cap)
{
    (void)sl;
    /* Emits the BARE Notification message body (no field tag): the
     * caller wraps it as SubscribeResponse.update. */
    gnmi_pb_t w;
    gnmi_pb_init(&w, resp, resp_cap);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    gnmi_pb_put_uint64(&w, 1, (uint64_t)ts.tv_sec * 1000000000ULL +
                                  (uint64_t)ts.tv_nsec);

    for (uint32_t i = 0; i < path_count; i++) {
        const gnmi_path_t *p = &paths[i];
        if (p->elem_count == 0) continue;
        const char *top = p->elems[0].name;

        if (strcmp(top, "interfaces") == 0) {
            danos_iface_t ifaces[64];
            uint32_t n = collect_objects(NULL, DANOS_OBJ_IFACE, ifaces,
                                         sizeof(ifaces[0]), 64);
            for (uint32_t j = 0; j < n; j++) {
                if (p->elem_count >= 2 && p->elems[1].has_key &&
                    strcmp(p->elems[1].key_value, ifaces[j].name) != 0)
                    continue;
                char json[256];
                obj_to_json(DANOS_OBJ_IFACE, &ifaces[j], sizeof(ifaces[j]),
                            json, sizeof(json));
                size_t us = gnmi_pb_begin_nested(&w, 4);
                gnmi_encode_path(&w, 1, p);
                gnmi_typed_value_t v = { .kind = GNMI_VAL_JSON_IETF };
                snprintf(v.s, sizeof(v.s), "%s", json);
                gnmi_encode_typed_value(&w, 3, &v);
                gnmi_pb_end_nested(&w, us);
            }
        } else if (strcmp(top, "vrfs") == 0) {
            danos_vrf_t vrfs[16];
            uint32_t n = collect_objects(NULL, DANOS_OBJ_VRF, vrfs,
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
            uint32_t n = collect_objects(NULL, DANOS_OBJ_ROUTE, routes,
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
    return w.overflow ? -1 : (int)w.len;
}

/* STREAM mode: push notifications when the store changes. Poll every
 * 100 ms; detect client GOAWAY/RST/close via recv with MSG_PEEK. */
static int subscribe_stream_loop(h2_conn_t *c, uint32_t stream,
                                 const gnmi_subscribe_list_t *sl)
{
    uint64_t last_hash = 0;
    bool first = true;

    for (;;) {
        /* check peer liveness + consume control frames */
        uint8_t probe[16];
        ssize_t n = recv(c->fd, probe, sizeof(probe),
                         MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) return 0;   /* client closed */

        /* content hash: count+sizes per type (cheap change detector) */
        uint64_t hash = 0;
        {
            danos_iface_t ifaces[64];
            danos_vrf_t vrfs[16];
            danos_route_t routes[32];
            uint32_t ni = collect_objects(NULL, DANOS_OBJ_IFACE, ifaces,
                                          sizeof(ifaces[0]), 64);
            uint32_t nv = collect_objects(NULL, DANOS_OBJ_VRF, vrfs,
                                          sizeof(vrfs[0]), 16);
            uint32_t nr = collect_objects(NULL, DANOS_OBJ_ROUTE, routes,
                                          sizeof(routes[0]), 32);
            hash = ((uint64_t)ni << 40) ^ ((uint64_t)nv << 20) ^ nr;
            for (uint32_t i = 0; i < ni; i++)
                hash ^= (uint64_t)ifaces[i].mtu * 31 + ifaces[i].ifindex;
        }

        if (first || hash != last_hash) {
            gnmi_path_t paths[GNMI_MAX_ELEMS];
            uint32_t pc = 0;
            for (uint32_t i = 0; i < sl->sub_count && pc < GNMI_MAX_ELEMS; i++)
                paths[pc++] = sl->subs[i].path;

            uint8_t msg[8192];
            int mlen = build_notification_for_paths(sl, paths, pc,
                                                    msg, sizeof(msg));
            if (mlen < 0) return -1;

            if (first) {
                if (send_stream_headers(c, stream) < 0) return -1;
                first = false;
            }
            uint8_t resp[16384];
            gnmi_pb_t w;
            gnmi_pb_init(&w, resp, sizeof(resp));
            /* SubscribeResponse: field1 = Notification bytes */
            gnmi_pb_put_len_delim(&w, 1, msg, (size_t)mlen);
            if (send_stream_message(c, stream, resp, w.len) < 0) return -1;
            last_hash = hash;
        }
        usleep(100000);
    }
}

int gnmi_handle_subscribe(void *opaque, uint32_t stream,
                          const uint8_t *req, size_t req_len)
{
    h2_conn_t *c = (h2_conn_t *)opaque;
    gnmi_subscribe_request_t sr;
    if (!gnmi_decode_subscribe_request(req, req_len, &sr)) return -1;

    const gnmi_subscribe_list_t *sl = &sr.subscribe;

    if (sl->mode == GNMI_SUB_MODE_ONCE) {
        gnmi_path_t paths[GNMI_MAX_ELEMS];
        uint32_t pc = 0;
        for (uint32_t i = 0; i < sl->sub_count && pc < GNMI_MAX_ELEMS; i++)
            paths[pc++] = sl->subs[i].path;

        uint8_t notif[8192];
        int mlen = build_notification_for_paths(sl, paths, pc,
                                                notif, sizeof(notif));
        if (mlen < 0) return -1;

        uint8_t resp[16384];
        gnmi_pb_t w;
        gnmi_pb_init(&w, resp, sizeof(resp));
        gnmi_pb_put_len_delim(&w, 1, notif, (size_t)mlen);

        if (send_stream_headers(c, stream) < 0) return -1;
        /* update first, then a separate sync_response message: clients
         * (gnmic) exit on sync and would drop a same-message update */
        if (send_stream_message(c, stream, resp, w.len) < 0) return -1;
        gnmi_pb_t sw;
        uint8_t sresp[16];
        gnmi_pb_init(&sw, sresp, sizeof(sresp));
        gnmi_encode_subscribe_sync(&sw);
        if (send_stream_message(c, stream, sresp, sw.len) < 0) return -1;
        if (send_stream_trailers(c, stream) < 0) return -1;
        return 0;
    }

    if (sl->mode == GNMI_SUB_MODE_POLL) {
        /* POLL: one notification per Poll message; v0.4 sends the
         * initial set and sync, then waits for poll requests on the
         * same stream (simplified: treat as ONCE + keep open until
         * client closes). */
        gnmi_path_t paths[GNMI_MAX_ELEMS];
        uint32_t pc = 0;
        for (uint32_t i = 0; i < sl->sub_count && pc < GNMI_MAX_ELEMS; i++)
            paths[pc++] = sl->subs[i].path;
        uint8_t notif[8192];
        int mlen = build_notification_for_paths(sl, paths, pc,
                                                notif, sizeof(notif));
        if (mlen < 0) return -1;
        uint8_t resp[16384];
        gnmi_pb_t w;
        gnmi_pb_init(&w, resp, sizeof(resp));
        gnmi_pb_put_len_delim(&w, 1, notif, (size_t)mlen);
        gnmi_encode_subscribe_sync(&w);
        if (send_stream_headers(c, stream) < 0) return -1;
        if (send_stream_message(c, stream, resp, w.len) < 0) return -1;
        return 0;   /* leave stream open; served until connection ends */
    }

    return subscribe_stream_loop(c, stream, sl);
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
        /* model registry: unknown paths are an error, not a silent skip */
        gnmi_model_binding_t mb;
        if (gnmi_model_resolve(p, &mb) != DANOS_OK) return -(int)DANOS_ERR_NOT_FOUND;
        if (p->elem_count == 0) continue;
        const char *top = p->elems[0].name;

        if (mb.kind == GNMI_MODEL_LEAF) {
            /* single field of a single object */
            uint64_t key = 0;
            danos_iface_t ifc;
            danos_vrf_t vrf;
            const void *obj = NULL; size_t osz = 0;
            if (mb.obj_type == DANOS_OBJ_IFACE) {
                danos_iface_t all[1024];
                uint32_t cnt = collect_objects(ss, DANOS_OBJ_IFACE, all,
                                               sizeof(all[0]), 1024);
                const char *want = p->elems[1].key_value;
                for (uint32_t j = 0; j < cnt; j++) {
                    if (strcmp(all[j].name, want) == 0) {
                        ifc = all[j]; obj = &ifc; osz = sizeof(ifc);
                        break;
                    }
                }
            } else if (mb.obj_type == DANOS_OBJ_VRF) {
                danos_vrf_t all[64];
                uint32_t cnt = collect_objects(ss, DANOS_OBJ_VRF, all,
                                               sizeof(all[0]), 64);
                uint64_t want = strtoul(p->elems[1].key_value, NULL, 10);
                for (uint32_t j = 0; j < cnt; j++) {
                    if (all[j].vrf_id == want) {
                        vrf = all[j]; obj = &vrf; osz = sizeof(vrf);
                        break;
                    }
                }
            }
            if (!obj) return DANOS_ERR_NOT_FOUND;
            gnmi_typed_value_t lv;
            if (gnmi_model_read_leaf(mb.obj_type, key, mb.field,
                                     obj, osz, &lv) != DANOS_OK)
                return DANOS_ERR_INVALID_ARG;
            size_t us = gnmi_pb_begin_nested(&w, 4);
            gnmi_encode_path(&w, 1, p);
            gnmi_encode_typed_value(&w, 3, &lv);
            gnmi_pb_end_nested(&w, us);
            continue;
        }

        if (strcmp(top, "interfaces") == 0) {
            danos_iface_t ifaces[1024];
            uint32_t n = collect_objects(ss, DANOS_OBJ_IFACE, ifaces,
                                         sizeof(ifaces[0]), 1024);
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
                /* entry filter: /routes/route[prefix=X] */
                if (p->elem_count >= 2 && p->elems[1].has_key) {
                    char want[64];
                    snprintf(want, sizeof(want), "%s",
                             p->elems[1].key_value);
                    char pfx[64];
                    if (routes[j].prefix.addr.af == DANOS_AF_IPV4) {
                        snprintf(pfx, sizeof(pfx), "%u.%u.%u.%u/%u",
                                 routes[j].prefix.addr.addr[0],
                                 routes[j].prefix.addr.addr[1],
                                 routes[j].prefix.addr.addr[2],
                                 routes[j].prefix.addr.addr[3],
                                 routes[j].prefix.prefix_len);
                    } else {
                        continue;
                    }
                    if (strcmp(pfx, want) != 0) continue;
                }
                char json[256];
                gnmi_route_to_json(&routes[j], json, sizeof(json));
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

static int json_get_str(const char *body, const char *key,
                        char *out, size_t cap)
{
    const char *p = strstr(body, key);
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p = strchr(p, '"');
    if (!p) return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < cap - 1) out[i++] = *p++;
    out[i] = '\0';
    return 0;
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

        /* composite route write: /routes/route[prefix=X] val=json
         * {"gateway":"a.b.c.d","oif":N,"vrf":N} (v0.12) */
        if (u->path.elem_count == 2 &&
            strcmp(u->path.elems[0].name, "routes") == 0 &&
            strcmp(u->path.elems[1].name, "route") == 0 &&
            u->path.elems[1].has_key) {
            danos_ip_prefix_t prefix;
            danos_status_t pst = gnmi_parse_prefix(
                u->path.elems[1].key_value, &prefix);
            if (pst != DANOS_OK) { status = pst; break; }
            if (u->val.kind != GNMI_VAL_JSON &&
                u->val.kind != GNMI_VAL_JSON_IETF) {
                status = DANOS_ERR_INVALID_ARG;
                break;
            }
            danos_ip_addr_t gw;
            char gwbuf[64] = {0};
            if (json_get_str(u->val.s, "gateway", gwbuf, sizeof(gwbuf)) != 0) {
                status = DANOS_ERR_INVALID_ARG;   /* gateway required */
                break;
            }
            pst = gnmi_parse_ipv4(gwbuf, &gw);
            if (pst != DANOS_OK) { status = pst; break; }
            unsigned oif = 0, vrf = 0;
            (void)json_get_uint(u->val.s, "oif", &oif);
            (void)json_get_uint(u->val.s, "vrf", &vrf);
            pst = gnmi_route_set((danos_vrf_id_t)vrf, &prefix, &gw, oif);
            if (pst != DANOS_OK) { status = pst; break; }
            out.result_paths[out.result_count++] = u->path;
            out.ops[out.result_count - 1] = GNMI_OP_UPDATE;
            continue;
        }

        gnmi_model_binding_t mb;
        bool model_leaf = (gnmi_model_resolve(&u->path, &mb) == DANOS_OK &&
                           mb.kind == GNMI_MODEL_LEAF);
        if (!model_leaf && (u->path.elem_count < 2 ||
            strcmp(u->path.elems[0].name, "interfaces") != 0 ||
            strcmp(u->path.elems[1].name, "interface") != 0 ||
            !u->path.elems[1].has_key)) {
            status = DANOS_ERR_INVALID_ARG;
            break;
        }
        if (model_leaf) {
            /* leaf update: read-modify-write the named object */
            if (mb.field == GNMI_FIELD_LINK_UP || !mb.config_tree) {
                /* state tree is read-only */
                if (!mb.config_tree) { status = DANOS_ERR_INVALID_ARG; break; }
            }
            danos_iface_t all[1024];
            uint32_t cnt = collect_objects(ss, DANOS_OBJ_IFACE, all,
                                           sizeof(all[0]), 1024);
            const char *want = u->path.elems[1].key_value;
            danos_iface_t target;
            bool found = false;
            for (uint32_t j = 0; j < cnt; j++) {
                if (strcmp(all[j].name, want) == 0) {
                    target = all[j]; found = true; break;
                }
            }
            if (!found) { status = DANOS_ERR_NOT_FOUND; break; }
            danos_status_t ast = gnmi_model_apply_leaf(DANOS_OBJ_IFACE,
                                                       mb.field,
                                                       &target, sizeof(target),
                                                       &u->val);
            if (ast != DANOS_OK) { status = ast; break; }
            if (ss && ss->desired) {
                ast = danos_object_update(ss->desired, DANOS_OBJ_IFACE,
                                          target.ifindex, &target,
                                          sizeof(target));
            } else {
                ast = danos_object_update(g_default_store, DANOS_OBJ_IFACE,
                                          target.ifindex, &target,
                                          sizeof(target));
            }
            if (ast != DANOS_OK) { status = ast; break; }
            out.result_paths[out.result_count++] = u->path;
            out.ops[out.result_count - 1] = GNMI_OP_UPDATE;
            continue;
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

    /* delete: /routes/route[prefix=X] — composite cascade, or
     * interfaces/interface[name=X] */
    for (uint32_t i = 0; i < sr.delete_count && status == DANOS_OK; i++) {
        const gnmi_path_t *dp = &sr.deletes[i];
        if (dp->elem_count == 2 &&
            strcmp(dp->elems[0].name, "routes") == 0 &&
            strcmp(dp->elems[1].name, "route") == 0 &&
            dp->elems[1].has_key) {
            danos_ip_prefix_t prefix;
            danos_status_t pst = gnmi_parse_prefix(
                dp->elems[1].key_value, &prefix);
            if (pst != DANOS_OK) { status = pst; break; }
            pst = gnmi_route_delete(0, &prefix);
            if (pst != DANOS_OK) { status = pst; break; }
            out.result_paths[out.result_count++] = *dp;
            out.ops[out.result_count - 1] = GNMI_OP_DELETE;
            continue;
        }
        if (dp->elem_count < 2 ||
            strcmp(dp->elems[0].name, "interfaces") != 0 ||
            !dp->elems[1].has_key) {
            status = DANOS_ERR_INVALID_ARG;
            break;
        }
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
    size_t have = 0;      /* bytes of gRPC frame header collected */
    uint8_t ghdr[5];
    bool hdr_done = false;
    size_t need = 0;
    size_t got = 0;

    for (;;) {
        uint8_t ftype, fflags;
        uint32_t fstream;
        uint8_t frame[H2_MAX_FRAME_LEN];
        uint32_t flen = sizeof(frame);
        int rr = recv_frame(c, &ftype, &fflags, &fstream, frame, &flen);
        if (rr < 0) return -1;
        if (rr == 1) continue;
        uint32_t sid = fstream;

        if (ftype == H2_F_HEADERS) {
            if (fflags & H2_FLAG_END_STREAM)
                return hdr_done ? (int)got : GRPC_READ_NO_MSG;
            continue;
        }
        if (ftype == H2_F_GOAWAY || ftype == H2_F_RST) return -1;
        if (ftype != H2_F_DATA || sid != stream) {
            pending_push(c, ftype, fflags, sid, frame, flen);
            continue;
        }

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
    c.conn_window = H2_DEFAULT_WINDOW;
    c.peer_initial_window = H2_DEFAULT_WINDOW;
    for (int i = 0; i < 64; i++) c.stream_window[i] = H2_DEFAULT_WINDOW;
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
        uint8_t ftype, fflags;
        uint32_t fstream;
        uint8_t frame[H2_MAX_FRAME_LEN];
        uint32_t flen = sizeof(frame);
        int rr = recv_frame(&c, &ftype, &fflags, &fstream, frame, &flen);
        if (rr < 0) break;
        if (rr == 1) continue;
        if (ftype == H2_F_GOAWAY) break;
        if (ftype != H2_F_HEADERS) continue;

        if (!(fflags & H2_FLAG_END_HEADERS)) {
            /* CONTINUATION frames follow; fold them in */
            for (;;) {
                uint8_t ctype, cflags;
                uint32_t cstream;
                uint8_t cframe[H2_MAX_FRAME_LEN];
                uint32_t clen = sizeof(cframe);
                int cr = recv_frame(&c, &ctype, &cflags, &cstream, cframe, &clen);
                if (cr < 0) {
                    rc = -1;
                    goto out;
                }
                if (cr == 1) continue;
                if (ctype != H2_F_CONT) {
                    pending_push(&c, ctype, cflags, cstream, cframe, clen);
                    continue;
                }
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
        } else if (h.path && strcmp(h.path, "/gnmi.gNMI/Subscribe") == 0) {
            int sret = end_stream ? -1
                       : gnmi_handle_subscribe(&c, fstream, msg, msg_len);
            if (sret == 0) { headers_free(&h); continue; }  /* stream done */
            headers_free(&h);
            rc = -1;
            break;
        } else {
            /* unknown method: grpc-status 12 (unimplemented);
             * trailers-only responses must still carry :status */
            headers_free(&h);
            uint8_t tbuf[96];
            size_t tlen = 0;
            int k = hpack_encode_literal(tbuf, sizeof(tbuf) - tlen,
                                         ":status", "200");
            tlen += k;
            k = hpack_encode_literal(tbuf + tlen, sizeof(tbuf) - tlen,
                                     "content-type", "application/grpc");
            tlen += k;
            k = hpack_encode_literal(tbuf + tlen, sizeof(tbuf) - tlen,
                                     "grpc-status", "12");
            tlen += k;
            write_frame(fd, H2_F_HEADERS,
                        H2_FLAG_END_STREAM | H2_FLAG_END_HEADERS,
                        fstream, tbuf, tlen);
            continue;
        }

        if (rlen >= 0) {
            send_grpc_response(&c, fstream, resp, (size_t)rlen);
        } else {
            /* map DPA status -> gRPC status: NOT_FOUND(2) -> 5,
             * INVALID_ARG(1) -> 3, everything else -> 2 (Unknown) */
            const char *gs = "2";
            if (rlen == -(int)DANOS_ERR_NOT_FOUND) gs = "5";
            else if (rlen == -(int)DANOS_ERR_INVALID_ARG) gs = "3";
            uint8_t tbuf[128];
            size_t tlen = 0;
            int k = hpack_encode_literal(tbuf, sizeof(tbuf) - tlen,
                                         ":status", "200");
            tlen += k;
            k = hpack_encode_literal(tbuf + tlen, sizeof(tbuf) - tlen,
                                     "content-type", "application/grpc");
            tlen += k;
            k = hpack_encode_literal(tbuf + tlen, sizeof(tbuf) - tlen,
                                     "grpc-status", gs);
            tlen += k;
            write_frame(fd, H2_F_HEADERS,
                        H2_FLAG_END_STREAM | H2_FLAG_END_HEADERS,
                        fstream, tbuf, tlen);
        }
        headers_free(&h);
    }
out:
    pending_free_all(&c);
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
        listen(ctx->listen_fd, 64) < 0) {
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
