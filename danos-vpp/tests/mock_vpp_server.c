/*
 * Mock VPP server for protocol conformance tests (v0.3).
 */

#include "mock_vpp_server.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

/* =========================================================================
 * Binary API mock server
 * ========================================================================= */

static struct {
    int listen_fd;
    pthread_t thread;
    bool running;
    uint16_t last_msg_id;
    uint8_t last_body[4096];
    uint32_t last_body_len;
} g_api_srv;

/* Send one VPP socket message: [msgbuf_t header][u16 BE msg_id][body]. */
static int send_frame(int fd, uint16_t msg_id, const uint8_t *body, uint32_t n)
{
    uint32_t frame = 2 + n;
    uint8_t hdr[16] = {0};
    hdr[8] = (uint8_t)(frame >> 24); hdr[9] = (uint8_t)(frame >> 16);
    hdr[10] = (uint8_t)(frame >> 8); hdr[11] = (uint8_t)frame;
    uint8_t id[2] = {(uint8_t)(msg_id >> 8), (uint8_t)msg_id};
    if (send(fd, hdr, sizeof(hdr), MSG_NOSIGNAL) != (ssize_t)sizeof(hdr)) return -1;
    if (send(fd, id, sizeof(id), MSG_NOSIGNAL) != (ssize_t)sizeof(id)) return -1;
    if (n && send(fd, body, n, MSG_NOSIGNAL) != (ssize_t)n) return -1;
    return 0;
}

static int recv_exact(int fd, void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n) {
        ssize_t k = recv(fd, b, n, 0);
        if (k <= 0) return -1;
        b += k; n -= (uint32_t)k;
    }
    return 0;
}

static void serve_client(int fd)
{
    /* 1. expect sockclnt_create (msg_id 0xF) */
    uint8_t lhdr[16];
    if (recv_exact(fd, lhdr, sizeof(lhdr)) < 0) return;
    uint32_t frame_len = ((uint32_t)lhdr[8] << 24) | ((uint32_t)lhdr[9] << 16) |
                         ((uint32_t)lhdr[10] << 8) | lhdr[11];
    if (frame_len < 2 || frame_len > 4096) return;
    uint8_t frame[4096];
    if (recv_exact(fd, frame, frame_len) < 0) return;
    uint16_t msg_id = ((uint16_t)frame[0] << 8) | frame[1];
    if (msg_id != 0x000F) return;

    /* 2. reply: sockclnt_create_reply with message table */
    struct { const char *name; uint16_t id; } table[] = {
        { "control_ping",           MOCK_MSGID_CONTROL_PING },
        { "sw_interface_set_flags", MOCK_MSGID_SW_IF_SET_FLAGS },
        { "ip_route_add_del",       MOCK_MSGID_IP_ROUTE_ADD_DEL },
        { "ip_table_add_del",       MOCK_MSGID_IP_TABLE_ADD_DEL },
        { "ip_neighbor_add_del",    MOCK_MSGID_IP_NEIGHBOR_ADD_DEL },
    };
    uint8_t body[1024];
    uint32_t n = 0;
    uint32_t v;
    v = 0x1234; memcpy(body + n, &((uint8_t[4]){0,0,0x12,0x34})[0], 0); /* placeholder */
    /* client_index u32 BE */
    body[n++] = 0; body[n++] = 0; body[n++] = 0x12; body[n++] = 0x34;
    /* context u32 BE */
    body[n++] = 0; body[n++] = 0; body[n++] = 0; body[n++] = 1;
    /* response i32 = 0 */
    body[n++] = 0; body[n++] = 0; body[n++] = 0; body[n++] = 0;
    /* index u32 */
    body[n++] = 0; body[n++] = 0; body[n++] = 0; body[n++] = 0;
    /* count u16 */
    body[n++] = 0; body[n++] = (uint8_t)(sizeof(table) / sizeof(table[0]));
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        size_t len = strlen(table[i].name);
        body[n++] = (uint8_t)(table[i].id >> 8);
        body[n++] = (uint8_t)table[i].id;
        memset(body + n, 0, 64);
        memcpy(body + n, table[i].name, len);
        n += 64;
    }
    (void)v;
    send_frame(fd, 0x0010, body, n);

    /* 3. echo replies for subsequent requests */
    for (;;) {
        if (recv_exact(fd, lhdr, sizeof(lhdr)) < 0) return;
        frame_len = ((uint32_t)lhdr[8] << 24) | ((uint32_t)lhdr[9] << 16) |
                    ((uint32_t)lhdr[10] << 8) | lhdr[11];
        if (frame_len < 2 || frame_len > sizeof(frame)) return;
        if (recv_exact(fd, frame, frame_len) < 0) return;
        uint16_t rid = ((uint16_t)frame[0] << 8) | frame[1];
        uint32_t rbody = frame_len - 2;

        g_api_srv.last_msg_id = rid;
        g_api_srv.last_body_len = rbody > sizeof(g_api_srv.last_body)
                                      ? sizeof(g_api_srv.last_body) : rbody;
        if (rbody) memcpy(g_api_srv.last_body, frame + 2, g_api_srv.last_body_len);

        /* reply: [u32 context][i32 retval=0] with the request's context */
        if (rbody >= 8) {
            uint8_t rep[8];
            /* request body: client_index(u32) context(u32) ... */
            rep[0] = frame[6]; rep[1] = frame[7]; rep[2] = frame[8]; rep[3] = frame[9];
            rep[4] = 0; rep[5] = 0; rep[6] = 0; rep[7] = 0;
            send_frame(fd, (uint16_t)(rid + 1), rep, 8);
        } else {
            return;
        }
    }
}

static void *api_accept_loop(void *arg)
{
    (void)arg;
    while (g_api_srv.running) {
        int fd = accept(g_api_srv.listen_fd, NULL, NULL);
        if (fd < 0) break;
        struct timeval tv = { .tv_sec = 5 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        serve_client(fd);
        close(fd);
    }
    return NULL;
}

int mock_vpp_start(const char *path)
{
    memset(&g_api_srv, 0, sizeof(g_api_srv));
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    g_api_srv.listen_fd = fd;
    g_api_srv.running = true;
    if (pthread_create(&g_api_srv.thread, NULL, api_accept_loop, NULL) != 0) {
        close(fd);
        return -1;
    }
    return 0;
}

void mock_vpp_stop(void)
{
    if (!g_api_srv.running) return;
    g_api_srv.running = false;
    shutdown(g_api_srv.listen_fd, SHUT_RDWR);
    close(g_api_srv.listen_fd);
    pthread_join(g_api_srv.thread, NULL);
    g_api_srv.listen_fd = -1;
}

void mock_vpp_get_last_request(uint16_t *msg_id, uint8_t *body,
                               uint32_t *body_len, uint32_t max_body)
{
    if (msg_id) *msg_id = g_api_srv.last_msg_id;
    if (body_len) *body_len = g_api_srv.last_body_len;
    if (body && max_body) {
        uint32_t n = g_api_srv.last_body_len;
        if (n > max_body) n = max_body;
        memcpy(body, g_api_srv.last_body, n);
    }
}

/* =========================================================================
 * Stat segment mock server
 * ========================================================================= */

static struct {
    int listen_fd;
    pthread_t thread;
    bool running;
    int mem_fd;
    void *mem;
    size_t size;
} g_stat_srv;

/* Segment layout we build (matches vpp_stat.c parser):
 *   0:   shared header (40 bytes)
 *   40:  directory vector: [u32 count][entries...]  (entry = 32 bytes)
 *   ...: entry name strings (NUL-terminated)
 *   ...: offset_vector for simple counter: [u32 n_workers][u64 offsets...]
 *   ...: per-worker counter arrays (u64 values)
 */
#define STAT_SEG_SIZE   4096
#define DIR_OFF         64
#define NAME_OFF        256
#define OFFVEC_OFF      512
#define CNT0_OFF        640
#define CNT1_OFF        768

static void build_segment(void)
{
    g_stat_srv.mem_fd = memfd_create("mock_statseg", 0);
    if (g_stat_srv.mem_fd < 0) return;
    if (ftruncate(g_stat_srv.mem_fd, STAT_SEG_SIZE) < 0) return;
    g_stat_srv.mem = mmap(NULL, STAT_SEG_SIZE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, g_stat_srv.mem_fd, 0);
    if (g_stat_srv.mem == MAP_FAILED) {
        g_stat_srv.mem = NULL;
        return;
    }
    g_stat_srv.size = STAT_SEG_SIZE;
    uint8_t *m = g_stat_srv.mem;
    memset(m, 0, STAT_SEG_SIZE);

    /* header */
    uint64_t *q;
    uint32_t version = 2, le = 1;
    memcpy(m + 0, &version, 4);
    memcpy(m + 4, &le, 4);
    q = (uint64_t *)(m + 8);  *q = 1;        /* epoch */
    q = (uint64_t *)(m + 16); *q = 0;        /* in_progress */
    q = (uint64_t *)(m + 24); *q = DIR_OFF + 4; /* directory_offset (data after u32 len) */
    q = (uint64_t *)(m + 32); *q = 0;        /* error_offset */

    /* directory: 2 entries, each 32 bytes:
     * u32 type; u32 pad; u64 value; u64 offset_vector; u64 name */
    uint32_t cnt = 2;
    memcpy(m + DIR_OFF, &cnt, 4);
    uint8_t *dir = m + DIR_OFF + 4;
    struct { uint32_t type, pad; uint64_t value, offvec, name; } e;
    /* entry 0: SCALAR /sys/node/ip4-input = 1000000 */
    e.type = 1; e.pad = 0; e.value = 1000000; e.offvec = 0; e.name = NAME_OFF;
    memcpy(dir, &e, 32);
    /* entry 1: SIMPLE_COUNTER /if/0/rx-packets, index 0, 2 workers.
     * offset_vector points at the data (length u32 sits 4 bytes before). */
    e.type = 2; e.pad = 0; e.value = 0; e.offvec = OFFVEC_OFF + 4; e.name = NAME_OFF + 32;
    memcpy(dir + 32, &e, 32);

    strcpy((char *)m + NAME_OFF, "/sys/node/ip4-input");
    strcpy((char *)m + NAME_OFF + 32, "/if/0/rx-packets");

    /* offset vector: 2 workers -> [u32 2][u64 CNT0_OFF][u64 CNT1_OFF] */
    uint32_t nw = 2;
    memcpy(m + OFFVEC_OFF, &nw, 4);
    q = (uint64_t *)(m + OFFVEC_OFF + 4);
    q[0] = CNT0_OFF;
    q[1] = CNT1_OFF;
    /* worker counter arrays: counter[0] */
    q = (uint64_t *)(m + CNT0_OFF); *q = 3000;
    q = (uint64_t *)(m + CNT1_OFF); *q = 2000;
}

static void *stat_accept_loop(void *arg)
{
    (void)arg;
    while (g_stat_srv.running) {
        int fd = accept(g_stat_srv.listen_fd, NULL, NULL);
        if (fd < 0) break;

        /* send the segment fd via SCM_RIGHTS, one byte payload */
        char buf[1] = {0};
        struct iovec iov = { .iov_base = buf, .iov_len = 1 };
        char cbuf[CMSG_SPACE(sizeof(int))];
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof(cbuf);
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &g_stat_srv.mem_fd, sizeof(int));
        sendmsg(fd, &msg, 0);
        close(fd);
    }
    return NULL;
}

int mock_statseg_start(const char *path)
{
    memset(&g_stat_srv, 0, sizeof(g_stat_srv));
    build_segment();
    if (!g_stat_srv.mem) return -1;

    unlink(path);
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    g_stat_srv.listen_fd = fd;
    g_stat_srv.running = true;
    if (pthread_create(&g_stat_srv.thread, NULL, stat_accept_loop, NULL) != 0) {
        close(fd);
        return -1;
    }
    return 0;
}

void mock_statseg_stop(void)
{
    if (!g_stat_srv.running && !g_stat_srv.mem) return;
    if (g_stat_srv.running) {
        g_stat_srv.running = false;
        shutdown(g_stat_srv.listen_fd, SHUT_RDWR);
        close(g_stat_srv.listen_fd);
        pthread_join(g_stat_srv.thread, NULL);
    }
    if (g_stat_srv.mem) munmap(g_stat_srv.mem, g_stat_srv.size);
    if (g_stat_srv.mem_fd >= 0) close(g_stat_srv.mem_fd);
    memset(&g_stat_srv, 0, sizeof(g_stat_srv));
}
