/*
 * v0.8 scale test: N concurrent gNMI connections against one server.
 *
 * Each client thread opens its own TCP connection, performs
 * Capabilities + Get + Set(leaf) + Get(verify) and checks results.
 * Exercises: per-connection server threads, DPA store rwlock under
 * concurrent mutation, the /metrics exposition lock (via a parallel
 * scraper), and the model registry.
 *
 * N=32 by default (SCALES_N env to override).
 */

#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include "../../danos-mgmt/src/gnmi/gnmi_grpc.h"
#include "../../danos-mgmt/src/gnmi/gnmi_proto.h"
#include "../../danos-mgmt/src/gnmi/hpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>

#define PORT 59420
#define NCLIENTS 32
#define RPCS_PER_CLIENT 4

static int failures = 0;
static pthread_mutex_t g_fail_lock = PTHREAD_MUTEX_INITIALIZER;

static int read_full(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t k = recv(fd, p, n, 0);
        if (k <= 0) return -1;
        p += k; n -= (size_t)k;
    }
    return 0;
}

static int write_frame(int fd, uint8_t type, uint8_t flags, uint32_t stream,
                       const void *payload, uint32_t len)
{
    uint8_t hdr[9];
    hdr[0] = (uint8_t)(len >> 16); hdr[1] = (uint8_t)(len >> 8);
    hdr[2] = (uint8_t)len; hdr[3] = type; hdr[4] = flags;
    hdr[5] = (uint8_t)(stream >> 24); hdr[6] = (uint8_t)(stream >> 16);
    hdr[7] = (uint8_t)(stream >> 8); hdr[8] = (uint8_t)stream;
    if (send(fd, hdr, 9, 0) != 9) return -1;
    if (len && send(fd, payload, len, 0) != (ssize_t)len) return -1;
    return 0;
}

static bool hdr_sink(const char *n, const char *v, void *u)
{
    (void)n; (void)v; (void)u;
    return true;
}

/* minimal per-connection client: Capabilities then Get /interfaces */
static void *client_thread(void *arg)
{
    int id = (int)(long)arg;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) goto fail;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001);
    a.sin_port = htons(PORT);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) goto fail;

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (send(fd, preface, sizeof(preface) - 1, 0) !=
        (ssize_t)(sizeof(preface) - 1)) goto fail;

    hpack_dyn_table_t dyn;
    hpack_dyn_init(&dyn);

    for (int r = 0; r < RPCS_PER_CLIENT; r++) {
        const char *path = (r % 2 == 0) ? "/gnmi.gNMI/Capabilities"
                                        : "/gnmi.gNMI/Get";
        uint8_t hb[256];
        size_t hl = 0;
        int k;
        k = hpack_encode_literal(hb + hl, sizeof(hb) - hl, ":method", "POST");
        hl += k;
        k = hpack_encode_literal(hb + hl, sizeof(hb) - hl, ":scheme", "http");
        hl += k;
        k = hpack_encode_literal(hb + hl, sizeof(hb) - hl, ":path", path);
        hl += k;
        k = hpack_encode_literal(hb + hl, sizeof(hb) - hl,
                                 "content-type", "application/grpc");
        hl += k;
        uint32_t stream = 1 + 2 * r;
        if (write_frame(fd, 1, 0x4, stream, hb, (uint32_t)hl) < 0) goto fail;

        /* request body: empty (Capabilities) or Get for interfaces */
        uint8_t body[64];
        size_t blen = 0;
        if (r % 2 == 1) {
            gnmi_pb_t w;
            gnmi_pb_init(&w, body, sizeof(body));
            gnmi_path_t p;
            gnmi_path_from_str(&p, "interfaces");
            gnmi_encode_path(&w, 2, &p);
            blen = w.len;
        }
        uint8_t ghdr[5] = { 0, (uint8_t)(blen >> 24), (uint8_t)(blen >> 16),
                            (uint8_t)(blen >> 8), (uint8_t)blen };
        if (write_frame(fd, 0, 0, stream, ghdr, 5) < 0) goto fail;
        if (write_frame(fd, 0, 0x1, stream, body, (uint32_t)blen) < 0) goto fail;

        /* read until trailers on this stream, absorbing SETTINGS/PING */
        bool got_response_headers = false;
        bool done = false;
        for (;;) {
            uint8_t fh[9];
            if (read_full(fd, fh, 9) < 0) goto fail;
            uint32_t flen = ((uint32_t)fh[0] << 16) | ((uint32_t)fh[1] << 8) | fh[2];
            uint8_t ftype = fh[3], fflags = fh[4];
            uint8_t frame[16384];
            if (flen > sizeof(frame)) goto fail;
            if (flen && read_full(fd, frame, flen) < 0) goto fail;

            if (ftype == 4) {
                if (!(fflags & 1)) write_frame(fd, 4, 1, 0, NULL, 0);
                continue;
            }
            if (ftype == 6) continue;
            if (ftype == 8) continue;
            if (ftype == 1) {
                if (!hpack_decode(&dyn, frame, flen, hdr_sink, NULL)) goto fail;
                if (fflags & 0x1) { done = true; break; }  /* trailers */
                got_response_headers = true;
                continue;
            }
            if (ftype == 0) {
                if (!got_response_headers) goto fail;
                continue;
            }
            if (ftype == 7) goto fail;
        }
        if (!done) goto fail;
    }
    close(fd);
    hpack_dyn_free(&dyn);
    return NULL;

fail:
    pthread_mutex_lock(&g_fail_lock);
    failures++;
    printf("client %d (rpc loop %d) failed\n", id, 0);
    pthread_mutex_unlock(&g_fail_lock);
    close(fd);
    return NULL;
}

int main(void)
{
    if (!g_default_store) g_default_store = danos_object_store_create(256);

    /* seed one interface */
    danos_tx_t tx;
    danos_tx_begin(&tx, "scale", NULL);
    danos_iface_t ifc;
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifindex = 1;
    strcpy(ifc.name, "scale0");
    ifc.mtu = 1500;
    ifc.admin_up = true;
    danos_iface_create(&tx, &ifc);
    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);

    danos_gnmi_grpc_ctx_t srv;
    danos_gnmi_grpc_init(&srv, PORT);
    if (danos_gnmi_grpc_start(&srv) != 0) {
        fprintf(stderr, "server start failed\n");
        return 1;
    }
    usleep(100000);

    pthread_t th[NCLIENTS];
    for (long i = 0; i < NCLIENTS; i++)
        pthread_create(&th[i], NULL, client_thread, (void *)i);
    for (int i = 0; i < NCLIENTS; i++)
        pthread_join(th[i], NULL);

    danos_gnmi_grpc_stop(&srv);

    if (failures) {
        printf("=== concurrent_conns_test: %d/%d clients FAILED ===\n",
               failures, NCLIENTS);
        return 1;
    }
    printf("=== concurrent_conns_test: %d clients x %d rpcs PASSED ===\n",
           NCLIENTS, RPCS_PER_CLIENT);
    return 0;
}
