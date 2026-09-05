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

/* Build a ZAPI ROUTE_ADD message */
static int build_route_add(uint8_t *buf, size_t buf_size)
{
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,  /* vrf_id=0 */
        0x04,                      /* family=IPv4 */
        0x18,                      /* prefix_len=24 */
        0x0A, 0x00, 0x00, 0x00,   /* 10.0.0.0 */
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

    printf("mock_zebra listening on %s\n", sock_path);
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

        usleep(100000);  /* 100ms */

        /* Send ROUTE_ADD */
        n = build_route_add(buf, sizeof(buf));
        if (n > 0) {
            send(client_fd, buf, n, 0);
            printf("sent ROUTE_ADD (%d bytes)\n", n);
        }

        /* Keep connection open briefly */
        usleep(500000);  /* 500ms */
        close(client_fd);
        printf("client disconnected\n");
        break;  /* one client then exit */
    }

    close(g_server_fd);
    unlink(sock_path);
    printf("mock_zebra exited\n");
    return 0;
}
