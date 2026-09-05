/*
 * DANOS-Open VPP Backend: Binary API Client (D1)
 *
 * Implements VPP binary API client connection. In production, this
 * connects to VPP's shared-memory binary API (stat segment + msg queue).
 * For v0.1, we implement:
 *   - Connection management (connect/disconnect/reconnect)
 *   - Message send/receive framework
 *   - Mock mode (when VPP not available, records calls for testing)
 *   - Stat segment query
 */

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

typedef enum {
    VPP_API_DISCONNECTED = 0,
    VPP_API_CONNECTING   = 1,
    VPP_API_CONNECTED    = 2,
    VPP_API_MOCK         = 3,  /* mock mode (no VPP) */
} vpp_api_state_t;

typedef struct {
    int msg_fd;          /* binary API socket */
    int stat_fd;         /* stat segment socket */
    vpp_api_state_t state;
    char api_sock_path[256];
    char stat_sock_path[256];
    uint64_t last_connect_ns;
    uint32_t reconnect_delay_ms;
    uint64_t msgs_sent;
    uint64_t msgs_received;
    uint64_t connect_count;
    uint64_t reconnect_count;
    /* Mock mode: record last message */
    uint16_t last_msg_id;
    uint32_t last_msg_size;
} vpp_api_ctx_impl_t;

static vpp_api_ctx_impl_t g_ctx;
static bool g_initialized = false;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
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
    g_initialized = true;
    return 0;
}

/* Enable mock mode (for testing without VPP) */
void danos_vpp_api_enable_mock(void)
{
    if (!g_initialized) danos_vpp_api_init();
    g_ctx.state = VPP_API_MOCK;
    g_ctx.msg_fd = -1;
    g_ctx.stat_fd = -1;
}

/* Connect to VPP binary API */
int danos_vpp_api_connect(void)
{
    if (!g_initialized) danos_vpp_api_init();
    if (g_ctx.state == VPP_API_MOCK) return 0;

    /* Try binary API socket */
    g_ctx.msg_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_ctx.msg_fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(g_ctx.api_sock_path) >= sizeof(addr.sun_path)) {
        close(g_ctx.msg_fd);
        g_ctx.msg_fd = -1;
        return -1;
    }
    memcpy(addr.sun_path, g_ctx.api_sock_path, strlen(g_ctx.api_sock_path) + 1);

    if (connect(g_ctx.msg_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
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

/* Connect to stat segment */
int danos_vpp_api_connect_stat(void)
{
    if (g_ctx.state == VPP_API_MOCK) return 0;
    if (g_ctx.state != VPP_API_CONNECTED) return -1;

    g_ctx.stat_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_ctx.stat_fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(g_ctx.stat_sock_path) >= sizeof(addr.sun_path)) {
        close(g_ctx.stat_fd);
        g_ctx.stat_fd = -1;
        return -1;
    }
    memcpy(addr.sun_path, g_ctx.stat_sock_path, strlen(g_ctx.stat_sock_path) + 1);

    if (connect(g_ctx.stat_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_ctx.stat_fd);
        g_ctx.stat_fd = -1;
        return -1;
    }

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

/* Reconnect with backoff */
int danos_vpp_api_reconnect(void)
{
    if (g_ctx.state == VPP_API_MOCK) return 0;

    uint64_t now = now_ns();
    uint64_t elapsed = (now - g_ctx.last_connect_ns) / 1000000ULL;
    if (elapsed < g_ctx.reconnect_delay_ms) return -1;

    g_ctx.reconnect_count++;
    if (danos_vpp_api_connect() == 0) return 0;

    g_ctx.reconnect_delay_ms *= 2;
    if (g_ctx.reconnect_delay_ms > VPP_RECONNECT_MAX_MS)
        g_ctx.reconnect_delay_ms = VPP_RECONNECT_MAX_MS;
    g_ctx.last_connect_ns = now;
    return -1;
}

/* =========================================================================
 * Message send/receive
 * ========================================================================= */

/* Send a binary API message.
 * msg_id: VPP message ID
 * payload: message payload bytes
 * payload_size: payload length
 * Returns 0 on success, negative on error. */
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

    /* VPP binary API message header: [context:4][msg_id:2][payload] */
    uint32_t total = 4 + 2 + payload_size;
    uint8_t *buf = malloc(total);
    if (!buf) return -1;

    uint32_t context = (uint32_t)(g_ctx.msgs_sent + 1);
    memcpy(buf, &context, 4);
    memcpy(buf + 4, &msg_id, 2);
    if (payload_size > 0) memcpy(buf + 6, payload, payload_size);

    ssize_t sent = send(g_ctx.msg_fd, buf, total, 0);
    free(buf);

    if (sent != (ssize_t)total) return -1;
    g_ctx.msgs_sent++;
    return 0;
}

/* Receive a binary API response.
 * buf: buffer for response
 * buf_size: buffer size
 * Returns bytes received on success, negative on error. */
int danos_vpp_api_recv(uint8_t *buf, uint32_t buf_size)
{
    if (g_ctx.state == VPP_API_MOCK) {
        /* Mock: return empty response */
        if (buf_size < 6) return -1;
        memset(buf, 0, 6);
        g_ctx.msgs_received++;
        return 6;
    }

    if (g_ctx.state != VPP_API_CONNECTED || g_ctx.msg_fd < 0) return -1;

    ssize_t n = recv(g_ctx.msg_fd, buf, buf_size, 0);
    if (n <= 0) {
        danos_vpp_api_disconnect();
        return -1;
    }
    g_ctx.msgs_received++;
    return (int)n;
}

/* =========================================================================
 * Stat segment
 * ========================================================================= */

/* Query a stat segment counter by name.
 * Returns counter value, or 0 on error. */
uint64_t danos_vpp_api_stat_query(const char *name)
{
    if (!name) return 0;
    if (g_ctx.state == VPP_API_MOCK) {
        /* Mock: return hash of name as fake counter */
        uint64_t h = 0;
        for (const char *p = name; *p; p++) {
            h = h * 31 + (uint64_t)(unsigned char)*p;
        }
        return h;
    }
    /* In production: query stat segment shared memory */
    return 0;
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

void danos_vpp_api_get_stats(uint64_t *msgs_sent, uint64_t *msgs_received,
                              uint64_t *connect_count, uint64_t *reconnect_count)
{
    if (msgs_sent)        *msgs_sent = g_ctx.msgs_sent;
    if (msgs_received)    *msgs_received = g_ctx.msgs_received;
    if (connect_count)    *connect_count = g_ctx.connect_count;
    if (reconnect_count)  *reconnect_count = g_ctx.reconnect_count;
}

/* Get last mock message (for testing) */
void danos_vpp_api_get_last_mock_msg(uint16_t *msg_id, uint32_t *msg_size)
{
    if (msg_id)   *msg_id = g_ctx.last_msg_id;
    if (msg_size) *msg_size = g_ctx.last_msg_size;
}
