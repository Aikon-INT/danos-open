/*
 * DANOS-Open FIB: Zebra Session Management (C3)
 *
 * Manages the Unix socket connection to FRR zebra daemon.
 * Handles connect, reconnect with backoff, and message receive loop.
 */

#include "zapi/zapi.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <time.h>

#define ZAPI_SOCK_PATH "/var/run/frr/zebra.zserv"
#define RECONNECT_INITIAL_MS 1000
#define RECONNECT_MAX_MS    60000

typedef enum {
    ZEBRA_SESSION_DISCONNECTED = 0,
    ZEBRA_SESSION_CONNECTING   = 1,
    ZEBRA_SESSION_CONNECTED    = 2,
} zebra_session_state_t;

typedef struct {
    int fd;
    zebra_session_state_t state;
    char sock_path[256];
    uint64_t last_connect_ns;
    uint32_t reconnect_delay_ms;
    uint64_t messages_received;
    uint64_t reconnect_count;
} zebra_session_t;

static zebra_session_t g_session;
static bool g_initialized = false;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int danos_zebra_session_init(void)
{
    memset(&g_session, 0, sizeof(g_session));
    g_session.fd = -1;
    g_session.state = ZEBRA_SESSION_DISCONNECTED;
    snprintf(g_session.sock_path, sizeof(g_session.sock_path), "%s", ZAPI_SOCK_PATH);
    g_session.reconnect_delay_ms = RECONNECT_INITIAL_MS;
    g_initialized = true;
    return 0;
}

int danos_zebra_session_connect(void)
{
    if (!g_initialized) danos_zebra_session_init();

    g_session.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_session.fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(g_session.sock_path) >= sizeof(addr.sun_path)) return -1;
    memcpy(addr.sun_path, g_session.sock_path, strlen(g_session.sock_path) + 1);

    if (connect(g_session.fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_session.fd);
        g_session.fd = -1;
        g_session.state = ZEBRA_SESSION_DISCONNECTED;
        return -1;
    }

    g_session.state = ZEBRA_SESSION_CONNECTED;
    g_session.last_connect_ns = now_ns();
    g_session.reconnect_delay_ms = RECONNECT_INITIAL_MS;
    return 0;
}

void danos_zebra_session_disconnect(void)
{
    if (g_session.fd >= 0) {
        close(g_session.fd);
        g_session.fd = -1;
    }
    g_session.state = ZEBRA_SESSION_DISCONNECTED;
}

/* Attempt reconnect with exponential backoff.
 * Returns 0 on success, -1 if still disconnected. */
int danos_zebra_session_reconnect(void)
{
    uint64_t now = now_ns();
    uint64_t elapsed = (now - g_session.last_connect_ns) / 1000000ULL;

    if (elapsed < g_session.reconnect_delay_ms) return -1;  /* too soon */

    g_session.reconnect_count++;
    if (danos_zebra_session_connect() == 0) return 0;

    /* Backoff */
    g_session.reconnect_delay_ms *= 2;
    if (g_session.reconnect_delay_ms > RECONNECT_MAX_MS)
        g_session.reconnect_delay_ms = RECONNECT_MAX_MS;
    g_session.last_connect_ns = now;
    return -1;
}

/* Receive one ZAPI message from zebra.
 * Returns message size on success, 0 on disconnect, negative on error. */
int danos_zebra_session_recv(uint8_t *buf, size_t buf_size, zapi_message_t *out)
{
    if (g_session.state != ZEBRA_SESSION_CONNECTED) return -1;
    if (buf_size < ZAPI_HEADER_SIZE) return -2;

    /* Read header first */
    ssize_t n = recv(g_session.fd, buf, ZAPI_HEADER_SIZE, MSG_WAITALL);
    if (n <= 0) {
        danos_zebra_session_disconnect();
        return 0;
    }
    if (n != ZAPI_HEADER_SIZE) return -3;

    /* Parse header to get total length */
    if (zapi_parse(buf, ZAPI_HEADER_SIZE, out) != 0) return -4;
    uint32_t total = out->header.length;
    if (total > buf_size) return -5;

    /* Read remaining payload */
    size_t remaining = total - ZAPI_HEADER_SIZE;
    if (remaining > 0) {
        n = recv(g_session.fd, buf + ZAPI_HEADER_SIZE, remaining, MSG_WAITALL);
        if (n <= 0) {
            danos_zebra_session_disconnect();
            return 0;
        }
    }

    /* Re-parse complete message */
    if (zapi_parse(buf, total, out) != 0) return -6;
    g_session.messages_received++;
    return (int)total;
}

zebra_session_state_t danos_zebra_session_get_state(void)
{
    return g_session.state;
}

uint64_t danos_zebra_session_get_msg_count(void)
{
    return g_session.messages_received;
}
