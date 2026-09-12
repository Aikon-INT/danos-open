/*
 * DANOS-Open VPP Backend: Binary API Client (v0.3)
 *
 * Real implementation of the VPP binary API socket client:
 *   - Connect to VPP's AF_UNIX SOCK_STREAM binary API socket
 *     (default /run/vpp/api.sock).
 *   - Perform the legacy socket handshake: send sockclnt_create
 *     (fixed msg_id 0xF), receive sockclnt_create_reply which carries
 *     the client index and the full name->msg_id message table.
 *   - Send/receive length-framed big-endian messages with context
 *     correlation (danos_vpp_api_transact).
 *
 * Mock mode (danos_vpp_api_enable_mock) is retained for unit tests
 * and environments without VPP.
 *
 * Wire format reference: FDio VPP src/vlibmemory/memclnt.api and
 * src/vlibmemory/socket.c (see also vpp_wire.h).
 */

#include "vpp_wire.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define VPP_API_SOCK_PATH "/run/vpp/api.sock"
#define VPP_STAT_SOCK_PATH "/run/vpp/stats.sock"
#define VPP_RECONNECT_INITIAL_MS 500
#define VPP_RECONNECT_MAX_MS    30000
#define VPP_RECV_TIMEOUT_MS     5000
#define VPP_CLIENT_NAME "danos-open"

typedef enum {
    VPP_API_DISCONNECTED = 0,
    VPP_API_CONNECTING   = 1,
    VPP_API_CONNECTED    = 2,
    VPP_API_MOCK         = 3,  /* mock mode (no VPP) */
} vpp_api_state_t;

typedef struct {
    int msg_fd;          /* binary API socket */
    int stat_fd;         /* stat segment socket (SOCK_SEQPACKET) */
    vpp_api_state_t state;
    char api_sock_path[256];
    char stat_sock_path[256];
    uint64_t last_connect_ns;
    uint32_t reconnect_delay_ms;
    uint64_t msgs_sent;
    uint64_t msgs_received;
    uint64_t connect_count;
    uint64_t reconnect_count;
    uint32_t context;        /* monotonically increasing request context */
    uint32_t client_index;   /* assigned by VPP in sockclnt_create_reply */
    vpp_msg_table_t msg_table;
    /* Mock mode: record last message */
    uint16_t last_msg_id;
    uint32_t last_msg_size;
} vpp_api_ctx_impl_t;

/* Mock stat segment: named counters */
#define VPP_MOCK_STAT_MAX 64
typedef struct {
    char    name[128];
    uint64_t value;
    bool    used;
} vpp_mock_stat_t;

static vpp_api_ctx_impl_t g_ctx;
static vpp_mock_stat_t    g_mock_stats[VPP_MOCK_STAT_MAX];
static bool g_initialized = false;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Set a receive timeout so a dead VPP cannot hang the caller */
static void fd_set_timeout(int fd, int ms)
{
    struct timeval tv = { .tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* =========================================================================
 * Initialization and connection
 * ========================================================================= */

int danos_vpp_api_init(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.msg_fd = -1;
    g_ctx.stat_fd = -1;
    g_ctx.state = VPP_API_DISCONNECTED;
    snprintf(g_ctx.api_sock_path, sizeof(g_ctx.api_sock_path), "%s", VPP_API_SOCK_PATH);
    snprintf(g_ctx.stat_sock_path, sizeof(g_ctx.stat_sock_path), "%s", VPP_STAT_SOCK_PATH);
    g_ctx.reconnect_delay_ms = VPP_RECONNECT_INITIAL_MS;
    vpp_msg_table_init(&g_ctx.msg_table);
    g_initialized = true;
    return 0;
}

void danos_vpp_api_enable_mock(void)
{
    if (!g_initialized) danos_vpp_api_init();
    g_ctx.state = VPP_API_MOCK;
    g_ctx.msg_fd = -1;
    g_ctx.stat_fd = -1;
}

static int connect_unix_stream(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    memcpy(addr.sun_path, path, strlen(path) + 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    fd_set_timeout(fd, VPP_RECV_TIMEOUT_MS);
    return fd;
}

/* Perform the legacy socket handshake:
 * send sockclnt_create, parse sockclnt_create_reply, build the
 * name->msg_id table. Returns 0 on success. */
static int vpp_handshake(void)
{
    vpp_buf_t body;
    vpp_buf_init(&body, 96);
    vpp_buf_put_u32(&body, 1);                    /* context */
    vpp_buf_put_string(&body, VPP_CLIENT_NAME);   /* name[64], u8-len-prefixed */

    int rc = vpp_wire_send_fd(g_ctx.msg_fd, VPP_MSG_ID_SOCKCLNT_CREATE,
                              body.data, body.len);
    vpp_buf_free(&body);
    if (rc != 0) return -1;

    uint8_t rbuf[64 * 1024];
    int n = vpp_wire_recv_fd(g_ctx.msg_fd, rbuf, sizeof(rbuf));
    if (n <= 2) return -1;

    uint32_t frame_len = (uint32_t)n - 2;
    uint16_t reply_id = ((uint16_t)rbuf[0] << 8) | rbuf[1];
    if (reply_id != VPP_MSG_ID_SOCKCLNT_CREATE + 1) {
        /* reply ids follow request ids for the socket control range */
        return -1;
    }

    /* sockclnt_create_reply:
     *   u32 client_index; u32 context; i32 response; u32 index;
     *   u16 count; message_table[count] { u16 msg_id; string name; } */
    vpp_reader_t r;
    vpp_reader_init(&r, rbuf + 2, frame_len);
    uint32_t client_index = vpp_rd_u32(&r);
    (void)vpp_rd_u32(&r);   /* context */
    int32_t  response = (int32_t)vpp_rd_u32(&r);
    (void)vpp_rd_u32(&r);   /* index */
    uint16_t count = vpp_rd_u16(&r);
    if (!vpp_reader_ok(&r) || response != 0) return -1;

    vpp_msg_table_init(&g_ctx.msg_table);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t msg_id = vpp_rd_u16(&r);
        char *name = vpp_rd_string(&r, 71);
        if (!name || !vpp_reader_ok(&r)) {
            free(name);
            break;
        }
        vpp_msg_table_add(&g_ctx.msg_table, name, msg_id);
        free(name);
    }

    g_ctx.client_index = client_index;
    return 0;
}

int danos_vpp_api_connect(void)
{
    if (!g_initialized) danos_vpp_api_init();
    if (g_ctx.state == VPP_API_MOCK) return 0;

    g_ctx.msg_fd = connect_unix_stream(g_ctx.api_sock_path);
    if (g_ctx.msg_fd < 0) {
        g_ctx.state = VPP_API_DISCONNECTED;
        return -1;
    }

    if (vpp_handshake() != 0) {
        close(g_ctx.msg_fd);
        g_ctx.msg_fd = -1;
        g_ctx.state = VPP_API_DISCONNECTED;
        return -1;
    }

    g_ctx.state = VPP_API_CONNECTED;
    g_ctx.last_connect_ns = now_ns();
    g_ctx.reconnect_delay_ms = VPP_RECONNECT_INITIAL_MS;
    g_ctx.connect_count++;
    return 0;
}

void danos_vpp_api_disconnect(void)
{
    if (g_ctx.msg_fd >= 0) {
        close(g_ctx.msg_fd);
        g_ctx.msg_fd = -1;
    }
    if (g_ctx.stat_fd >= 0) {
        close(g_ctx.stat_fd);
        g_ctx.stat_fd = -1;
    }
    g_ctx.state = VPP_API_DISCONNECTED;
}

int danos_vpp_api_reconnect(void)
{
    if (g_ctx.state == VPP_API_MOCK) return 0;

    uint64_t now = now_ns();
    uint64_t elapsed = (now - g_ctx.last_connect_ns) / 1000000ULL;
    if (elapsed < g_ctx.reconnect_delay_ms) return -1;

    g_ctx.reconnect_count++;
    danos_vpp_api_disconnect();
    if (danos_vpp_api_connect() == 0) return 0;

    g_ctx.reconnect_delay_ms *= 2;
    if (g_ctx.reconnect_delay_ms > VPP_RECONNECT_MAX_MS)
        g_ctx.reconnect_delay_ms = VPP_RECONNECT_MAX_MS;
    g_ctx.last_connect_ns = now;
    return -1;
}

/* =========================================================================
 * Message send/receive and transactions
 * ========================================================================= */

int danos_vpp_api_send(uint16_t msg_id, const void *payload, uint32_t payload_size)
{
    if (g_ctx.state == VPP_API_MOCK) {
        g_ctx.last_msg_id = msg_id;
        g_ctx.last_msg_size = payload_size;
        g_ctx.msgs_sent++;
        return 0;
    }

    if (g_ctx.state != VPP_API_CONNECTED || g_ctx.msg_fd < 0) return -1;
    if (!payload && payload_size > 0) return -1;

    /* Every request starts with client_index + context (big-endian),
     * followed by message-specific fields. The caller-supplied payload
     * must NOT include them; we prepend them here. */
    vpp_buf_t body;
    vpp_buf_init(&body, payload_size + 8);
    vpp_buf_put_u32(&body, g_ctx.client_index);
    g_ctx.context++;
    vpp_buf_put_u32(&body, g_ctx.context);
    vpp_buf_put_bytes(&body, payload, payload_size);

    int rc = vpp_wire_send_fd(g_ctx.msg_fd, msg_id, body.data, body.len);
    vpp_buf_free(&body);
    if (rc != 0) return -1;
    g_ctx.msgs_sent++;
    return 0;
}

int danos_vpp_api_recv(uint8_t *buf, uint32_t buf_size)
{
    if (g_ctx.state == VPP_API_MOCK) {
        if (buf_size < 6) return -1;
        memset(buf, 0, 6);
        g_ctx.msgs_received++;
        return 6;
    }

    if (g_ctx.state != VPP_API_CONNECTED || g_ctx.msg_fd < 0) return -1;

    /* Return just the frame body (msg_id + struct) for compatibility
     * with existing callers; the 2-byte length prefix is consumed. */
    int n = vpp_wire_recv_fd(g_ctx.msg_fd, buf, buf_size);
    if (n <= 2) {
        danos_vpp_api_disconnect();
        return -1;
    }
    g_ctx.msgs_received++;
    return n - 2;
}

int danos_vpp_api_transact(uint16_t msg_id, const uint8_t *payload,
                           uint32_t payload_size, uint8_t *reply,
                           uint32_t reply_size)
{
    if (g_ctx.state == VPP_API_MOCK) {
        if (reply && reply_size >= 8) memset(reply, 0, 8);
        g_ctx.last_msg_id = msg_id;
        g_ctx.last_msg_size = payload_size;
        g_ctx.msgs_sent++;
        return 8;
    }
    if (danos_vpp_api_send(msg_id, payload, payload_size) != 0) return -1;

    /* Wait for the reply whose context matches our request. Replies:
     * [u32 context][i32 retval][message-specific...]. */
    uint8_t rbuf[64 * 1024];
    for (int attempt = 0; attempt < 64; attempt++) {
        int n = danos_vpp_api_recv(rbuf, sizeof(rbuf));
        if (n < 8) return -1;
        uint16_t reply_id = ((uint16_t)rbuf[0] << 8) | rbuf[1];
        vpp_reader_t r;
        vpp_reader_init(&r, rbuf + 2, (uint32_t)n - 2);
        uint32_t ctx = vpp_rd_u32(&r);  /* replies start with context */
        if (ctx == g_ctx.context) {
            if (reply) {
                uint32_t copy = (uint32_t)n;
                if (copy > reply_size) copy = reply_size;
                memcpy(reply, rbuf, copy);
            }
            (void)reply_id;
            return n;
        }
        /* Not ours (e.g. an async event): keep reading. */
    }
    return -1;
}

/* Resolve a message name to its dynamically assigned msg_id using the
 * table returned during the handshake. */
bool danos_vpp_api_lookup_msg_id(const char *name, uint16_t *msg_id)
{
    if (g_ctx.state == VPP_API_MOCK) {
        if (msg_id) *msg_id = 0;
        return true;
    }
    return vpp_msg_table_lookup(&g_ctx.msg_table, name, msg_id);
}

uint32_t danos_vpp_api_client_index(void)
{
    return g_ctx.client_index;
}

uint32_t danos_vpp_api_msg_table_count(void)
{
    return vpp_msg_table_count(&g_ctx.msg_table);
}

const char *danos_vpp_api_sock_path(void)
{
    return g_ctx.api_sock_path;
}

void danos_vpp_api_set_sock_path(const char *path)
{
    if (path) snprintf(g_ctx.api_sock_path, sizeof(g_ctx.api_sock_path), "%s", path);
}

void danos_vpp_api_set_stat_sock_path(const char *path)
{
    if (path) snprintf(g_ctx.stat_sock_path, sizeof(g_ctx.stat_sock_path), "%s", path);
    /* keep the stat client in sync */
    extern void danos_vpp_stat_set_sock_path(const char *path);
    danos_vpp_stat_set_sock_path(path);
}

/* =========================================================================
 * Stat segment
 * ========================================================================= */

/* The real stat segment client (SOCK_SEQPACKET + SCM_RIGHTS + mmap)
 * lives in vpp_stat.c; the query entry point keeps this name for
 * compatibility. */
int danos_vpp_stat_connect(void);
void danos_vpp_stat_disconnect(void);
uint64_t danos_vpp_stat_query_real(const char *name);
int danos_vpp_stat_list_real(const char **names, uint64_t *values, int max_entries);

int danos_vpp_api_connect_stat(void)
{
    if (g_ctx.state == VPP_API_MOCK) return 0;
    /* The stat segment is independent of the binary API channel. */
    return danos_vpp_stat_connect();
}

uint64_t danos_vpp_api_stat_query(const char *name)
{
    if (!name) return 0;
    if (g_ctx.state == VPP_API_MOCK) {
        for (int i = 0; i < VPP_MOCK_STAT_MAX; i++) {
            if (g_mock_stats[i].used &&
                strcmp(g_mock_stats[i].name, name) == 0) {
                return g_mock_stats[i].value;
            }
        }
        uint64_t h = 0;
        for (const char *p = name; *p; p++) {
            h = h * 31 + (uint64_t)(unsigned char)*p;
        }
        return h;
    }
    return danos_vpp_stat_query_real(name);
}

int danos_vpp_api_stat_list(const char **names, uint64_t *values, int max_entries)
{
    if (g_ctx.state == VPP_API_MOCK) {
        int count = 0;
        for (int i = 0; i < VPP_MOCK_STAT_MAX && count < max_entries; i++) {
            if (g_mock_stats[i].used) {
                if (names)  names[count] = g_mock_stats[i].name;
                if (values) values[count] = g_mock_stats[i].value;
                count++;
            }
        }
        return count;
    }
    return danos_vpp_stat_list_real(names, values, max_entries);
}

void danos_vpp_api_stat_set(const char *name, uint64_t value)
{
    if (!name) return;
    for (int i = 0; i < VPP_MOCK_STAT_MAX; i++) {
        if (g_mock_stats[i].used &&
            strcmp(g_mock_stats[i].name, name) == 0) {
            g_mock_stats[i].value = value;
            return;
        }
    }
    for (int i = 0; i < VPP_MOCK_STAT_MAX; i++) {
        if (!g_mock_stats[i].used) {
            snprintf(g_mock_stats[i].name, sizeof(g_mock_stats[i].name), "%s", name);
            g_mock_stats[i].value = value;
            g_mock_stats[i].used = true;
            return;
        }
    }
}

void danos_vpp_api_stat_reset(void)
{
    memset(g_mock_stats, 0, sizeof(g_mock_stats));
}

/* =========================================================================
 * Status and diagnostics
 * ========================================================================= */

vpp_api_state_t danos_vpp_api_get_state(void)
{
    return g_ctx.state;
}

bool danos_vpp_api_is_connected(void)
{
    return g_ctx.state == VPP_API_CONNECTED || g_ctx.state == VPP_API_MOCK;
}

bool danos_vpp_api_is_mock(void)
{
    return g_ctx.state == VPP_API_MOCK;
}

void danos_vpp_api_get_stats(uint64_t *msgs_sent, uint64_t *msgs_received,
                              uint64_t *connect_count, uint64_t *reconnect_count)
{
    if (msgs_sent)        *msgs_sent = g_ctx.msgs_sent;
    if (msgs_received)    *msgs_received = g_ctx.msgs_received;
    if (connect_count)    *connect_count = g_ctx.connect_count;
    if (reconnect_count)  *reconnect_count = g_ctx.reconnect_count;
}

void danos_vpp_api_get_last_mock_msg(uint16_t *msg_id, uint32_t *msg_size)
{
    if (msg_id)   *msg_id = g_ctx.last_msg_id;
    if (msg_size) *msg_size = g_ctx.last_msg_size;
}
