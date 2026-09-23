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
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>

#define ZAPI_SOCK_PATH "/var/run/frr/zserv.api"
#define RECONNECT_INITIAL_MS 1000
#define RECONNECT_MAX_MS    60000
#define FRR_ZAPI_HEADER_SIZE 10
#define FRR_ZAPI_MARKER 254
#define FRR_ZAPI_VERSION 6
#define FRR_ZEBRA_HELLO 19
#define FRR_ZEBRA_INTERFACE_ADD 0
#define FRR_ZEBRA_ROUTE_ADD 9
#define FRR_ZEBRA_ROUTER_ID_ADD 16
#define FRR_ZEBRA_REDISTRIBUTE_ADD 12

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

static void trace_registration(uint16_t command, uint8_t afi, uint8_t type,
                               uint16_t instance, size_t length)
{
    const char *debug = getenv("DANOS_ZAPI_DEBUG");
    if (debug && debug[0] == '1')
        fprintf(stderr, "zapi tx command=%u afi=%u type=%u instance=%u length=%zu\n",
                command, afi, type, instance, length);
}

static size_t registration_route_types(uint8_t *types, size_t capacity)
{
    /* Keep the live FRR registration conservative.  Connected replay is
     * sufficient for the QEMU acceptance lane; requesting unrelated route
     * types can make older zebra builds close the zserv session. */
    const uint8_t defaults[] = { 2 };
    const char *value = getenv("DANOS_ZAPI_ROUTE_TYPES");
    if (!value || !value[0]) {
        memcpy(types, defaults, sizeof(defaults));
        return sizeof(defaults);
    }
    size_t count = 0;
    while (*value && count < capacity) {
        char *end = NULL;
        unsigned long parsed = strtoul(value, &end, 10);
        if (end == value || parsed > UINT8_MAX) break;
        types[count++] = (uint8_t)parsed;
        value = end;
        if (*value == ',') value++;
        else if (*value) break;
    }
    return count;
}

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

int danos_zebra_session_set_socket(const char *path)
{
    if (!path || path[0] == '\0' || strlen(path) >= sizeof(g_session.sock_path))
        return -1;
    if (!g_initialized)
        danos_zebra_session_init();
    snprintf(g_session.sock_path, sizeof(g_session.sock_path), "%s", path);
    return 0;
}

static int connect_zebra_endpoint(const char *endpoint)
{
    if (strncmp(endpoint, "tcp://", 6) == 0) {
        const char *host = endpoint + 6;
        const char *colon = strrchr(host, ':');
        if (!colon || colon == host || !colon[1]) return -1;
        char name[128], service[16];
        size_t n = (size_t)(colon - host);
        if (n >= sizeof(name) || strlen(colon + 1) >= sizeof(service)) return -1;
        memcpy(name, host, n); name[n] = 0;
        snprintf(service, sizeof(service), "%s", colon + 1);
        struct addrinfo hints = { .ai_socktype = SOCK_STREAM }, *res = NULL;
        if (getaddrinfo(name, service, &hints, &res) != 0) return -1;
        int fd = -1;
        for (struct addrinfo *it = res; it; it = it->ai_next) {
            fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd >= 0 && connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
            if (fd >= 0) { close(fd); fd = -1; }
        }
        freeaddrinfo(res);
        return fd;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(endpoint) >= sizeof(addr.sun_path)) { close(fd); return -1; }
    memcpy(addr.sun_path, endpoint, strlen(endpoint) + 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

int danos_zebra_session_connect(void)
{
    if (!g_initialized) danos_zebra_session_init();

    g_session.fd = connect_zebra_endpoint(g_session.sock_path);
    if (g_session.fd < 0) {
        g_session.fd = -1;
        g_session.state = ZEBRA_SESSION_DISCONNECTED;
        return -1;
    }

    g_session.state = ZEBRA_SESSION_CONNECTED;
    g_session.last_connect_ns = now_ns();
    g_session.reconnect_delay_ms = RECONNECT_INITIAL_MS;
    return 0;
}

/* Register this process as a real FRR zclient.  This wire format follows
 * FRR's public zclient_create_header()/zclient_send_hello() implementation;
 * it is intentionally separate from the legacy clean-room test framing.
 */
int danos_zebra_session_register(uint8_t protocol, uint16_t instance)
{
    if (g_session.state != ZEBRA_SESSION_CONNECTED) return -1;
    uint8_t msg[FRR_ZAPI_HEADER_SIZE + 8];
    uint16_t length = htons(sizeof(msg));
    uint32_t vrf = htonl(0);
    uint16_t command = htons(FRR_ZEBRA_HELLO);
    uint16_t inst = htons(instance);
    uint32_t session = htonl(0);
    memcpy(msg, &length, 2);
    msg[2] = FRR_ZAPI_MARKER;
    msg[3] = FRR_ZAPI_VERSION;
    memcpy(msg + 4, &vrf, 4);
    memcpy(msg + 8, &command, 2);
    msg[10] = protocol;
    memcpy(msg + 11, &inst, 2);
    memcpy(msg + 13, &session, 4);
    msg[17] = 0; /* asynchronous client */
    size_t sent = 0;
    while (sent < sizeof(msg)) {
        ssize_t n = send(g_session.fd, msg + sent, sizeof(msg) - sent, MSG_NOSIGNAL);
        if (n <= 0) { danos_zebra_session_disconnect(); return -1; }
        sent += (size_t)n;
    }
    trace_registration(FRR_ZEBRA_HELLO, 0, protocol, instance, sizeof(msg));
    if (getenv("DANOS_ZAPI_HELLO_ONLY")) return 0;
    /* Request router-id and interface replay, then route notifications for
     * the protocol families used by the v0.16 L3 acceptance. */
    uint8_t req[32];
    uint16_t req_len = htons(10);
    memcpy(req, &req_len, 2); req[2] = FRR_ZAPI_MARKER; req[3] = FRR_ZAPI_VERSION;
    memcpy(req + 4, &vrf, 4);
    uint16_t cmd = htons(FRR_ZEBRA_ROUTER_ID_ADD);
    memcpy(req + 8, &cmd, 2);
    if (send(g_session.fd, req, 10, MSG_NOSIGNAL) != 10) return -1;
    trace_registration(FRR_ZEBRA_ROUTER_ID_ADD, 0, 0, 0, 10);
    cmd = htons(FRR_ZEBRA_INTERFACE_ADD); memcpy(req + 8, &cmd, 2);
    if (send(g_session.fd, req, 10, MSG_NOSIGNAL) != 10) return -1;
    trace_registration(FRR_ZEBRA_INTERFACE_ADD, 0, 0, 0, 10);
    uint8_t route_types[16];
    size_t route_type_count = registration_route_types(route_types, sizeof(route_types));
    for (size_t route_i = 0; route_i < route_type_count; route_i++) {
            uint8_t route_type = route_types[route_i];
            /* FRR zclient's REDISTRIBUTE_ADD payload is only route_type;
             * AFI/instance fields here desynchronise zebra's stream. */
            req_len = htons(11); memcpy(req, &req_len, 2);
            cmd = htons(FRR_ZEBRA_REDISTRIBUTE_ADD); memcpy(req + 8, &cmd, 2);
            req[10] = route_type;
            if (send(g_session.fd, req, 11, MSG_NOSIGNAL) != 11) return -1;
            trace_registration(FRR_ZEBRA_REDISTRIBUTE_ADD, 0, route_type, 0, 11);
    }
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

int danos_zebra_session_recv_frr(uint8_t *buf, size_t buf_size, zapi_message_t *out)
{
    if (g_session.state != ZEBRA_SESSION_CONNECTED) return -1;
    if (buf_size < 10) return -2;
    struct pollfd pfd = { .fd = g_session.fd, .events = POLLIN };
    int ready = poll(&pfd, 1, 500);
    if (ready == 0) return 0;
    if (ready < 0) return errno == EINTR ? 0 : -3;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        danos_zebra_session_disconnect();
        return 0;
    }
    ssize_t n = recv(g_session.fd, buf, 10, MSG_WAITALL);
    if (n <= 0) { danos_zebra_session_disconnect(); return 0; }
    if (n != 10) return -3;
    uint16_t total;
    memcpy(&total, buf, 2);
    total = ntohs(total);
    if (total < 10 || total > buf_size) return -4;
    if (total > 10) {
        n = recv(g_session.fd, buf + 10, total - 10, MSG_WAITALL);
        if (n <= 0) { danos_zebra_session_disconnect(); return 0; }
        if ((size_t)n != (size_t)(total - 10)) return -5;
    }
    if (zapi_parse_frr(buf, total, out) != 0) return -6;
    g_session.messages_received++;
    return total;
}

int danos_zebra_session_get_state(void)
{
    return g_session.state;
}

uint64_t danos_zebra_session_get_msg_count(void)
{
    return g_session.messages_received;
}
