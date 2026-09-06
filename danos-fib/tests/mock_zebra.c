/*
 * DANOS-Open FIB: Mock Zebra Server (C4)
 *
 * A mock FRR zebra daemon that listens on a Unix socket and sends
 * ZAPI messages. Used for E2E testing of the FIB adapter without
 * requiring FRR to be installed.
 *
 * Usage: mock_zebra <socket_path>
 *   Listens on <socket_path>, sends a few ZAPI messages, then exits.
 */

#include "zapi/zapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>

static int g_server_fd = -1;
static bool g_running = true;

static void sighandler(int sig)
{
    (void)sig;
    g_running = false;
    if (g_server_fd >= 0) close(g_server_fd);
}

/* Build a ZAPI ROUTE_ADD message with configurable prefix */
static int build_route_add_n(uint8_t *buf, size_t buf_size, int route_idx)
{
    /* Use 10.X.Y.0/24 where X=high byte, Y=low byte for uniqueness up to 65536 */
    uint8_t prefix_hi = (uint8_t)((route_idx >> 8) & 0xFF);
    uint8_t prefix_lo = (uint8_t)(route_idx & 0xFF);
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,  /* vrf_id=0 */
        0x04,                      /* family=IPv4 */
        0x18,                      /* prefix_len=24 */
        0x0A, prefix_hi, prefix_lo, 0x00,   /* 10.X.Y.0 */
        0x01,                      /* proto=static */
        0x01,                      /* admin_dist=1 */
        0x00, 0x00, 0x00, 0x00,   /* metric=0 */
        0x00                       /* nh_count=0 */
    };

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_ROUTE_ADD;
    msg.payload = payload;
    msg.payload_size = sizeof(payload);

    return zapi_serialize(&msg, buf, buf_size);
}

/* Build a ZAPI INTERFACE_ADD message */
static int build_iface_add(uint8_t *buf, size_t buf_size)
{
    uint8_t payload[64];
    zapi_encoder_t enc;
    zapi_encoder_init(&enc, payload, sizeof(payload));
    zapi_encode_u32(&enc, 1);              /* ifindex */
    zapi_encode_u8(&enc, 4);               /* name_len */
    zapi_encode_bytes(&enc, (uint8_t*)"eth0", 4);
    zapi_encode_u32(&enc, 1500);           /* mtu */
    zapi_encode_bytes(&enc, (uint8_t*)"\x02\x00\x00\x00\x00\x01", 6);

    zapi_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.command = ZEBRA_INTERFACE_ADD;
    msg.payload = payload;
    msg.payload_size = enc.pos;

    return zapi_serialize(&msg, buf, buf_size);
}

int main(int argc, char *argv[])
{
    const char *sock_path = (argc > 1) ? argv[1] : "/tmp/mock_zebra.sock";
    int num_routes = 1;  /* default: 1 route */

    /* Parse optional --routes N argument */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--routes") == 0 && i + 1 < argc) {
            num_routes = atoi(argv[i + 1]);
            if (num_routes < 1) num_routes = 1;
            if (num_routes > 10000) num_routes = 10000;
            i++;
        }
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* Remove stale socket */
    unlink(sock_path);

    /* Create listening socket */
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(sock_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path too long\n");
        return 1;
    }
    memcpy(addr.sun_path, sock_path, strlen(sock_path) + 1);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(g_server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    printf("mock_zebra listening on %s (routes=%d)\n", sock_path, num_routes);
    fflush(stdout);

    while (g_running) {
        int client_fd = accept(g_server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        printf("client connected\n");

        /* Send INTERFACE_ADD */
        uint8_t buf[256];
        int n = build_iface_add(buf, sizeof(buf));
        if (n > 0) {
            send(client_fd, buf, n, 0);
            printf("sent INTERFACE_ADD (%d bytes)\n", n);
        }

        usleep(10000);  /* 10ms */

        /* Send N ROUTE_ADD messages */
        for (int r = 0; r < num_routes && g_running; r++) {
            n = build_route_add_n(buf, sizeof(buf), r);
            if (n > 0) {
                send(client_fd, buf, n, 0);
            }
        }
        printf("sent %d ROUTE_ADD messages\n", num_routes);

        /* Keep connection open until client closes or timeout */
        for (int w = 0; w < 50 && g_running; w++) {
            usleep(100000);  /* 100ms per iteration, 5s max */
            /* Check if client closed */
            uint8_t probe;
            ssize_t r = recv(client_fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
            if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                break;  /* client closed */
            }
        }
        close(client_fd);
        printf("client disconnected\n");
        break;  /* one client then exit */
    }

    close(g_server_fd);
    unlink(sock_path);
    printf("mock_zebra exited\n");
    return 0;
}
