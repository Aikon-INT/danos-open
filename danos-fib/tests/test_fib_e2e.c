/*
 * Test: FIB E2E with Mock Zebra (C4)
 *
 * Starts mock_zebra as a child process, connects via ZAPI session,
 * receives messages, and dispatches them to DPA operations.
 */

#include "../src/zapi/zapi.h"
#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>

#define MOCK_SOCK "/tmp/danos_mock_zebra.sock"
#define MOCK_BIN  "./mock_zebra"

static pid_t g_mock_pid = -1;

static int start_mock_zebra(void)
{
    g_mock_pid = fork();
    if (g_mock_pid < 0) return -1;
    if (g_mock_pid == 0) {
        /* child */
        execl(MOCK_BIN, MOCK_BIN, MOCK_SOCK, NULL);
        perror("execl");
        _exit(1);
    }
    /* parent: wait for socket to appear */
    usleep(200000);  /* 200ms */
    return 0;
}

static void stop_mock_zebra(void)
{
    if (g_mock_pid > 0) {
        kill(g_mock_pid, SIGTERM);
        int status;
        waitpid(g_mock_pid, &status, 0);
    }
    unlink(MOCK_SOCK);
}

static int connect_to_mock(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, MOCK_SOCK, strlen(MOCK_SOCK) + 1);

    /* Retry connect a few times */
    for (int i = 0; i < 10; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return fd;
        }
        usleep(100000);  /* 100ms */
    }

    close(fd);
    return -1;
}

int main(void)
{
    int failed = 0;

    /* Start mock zebra */
    if (start_mock_zebra() != 0) {
        printf("[FAIL] cannot start mock_zebra\n");
        return 1;
    }

    /* Connect to mock */
    int fd = connect_to_mock();
    if (fd < 0) {
        printf("[FAIL] cannot connect to mock_zebra\n");
        stop_mock_zebra();
        return 1;
    }

    printf("[OK] connected to mock_zebra\n");

    /* Receive and process messages */
    int msgs_processed = 0;
    int iface_msgs = 0;
    int route_msgs = 0;

    for (int i = 0; i < 2; i++) {
        uint8_t buf[4096];

        /* Read length prefix (4 bytes) */
        ssize_t n = recv(fd, buf, 4, MSG_WAITALL);
        if (n != 4) {
            printf("[FAIL] short read on length (%zd)\n", n);
            failed++;
            break;
        }

        uint32_t total_len;
        memcpy(&total_len, buf, 4);
        total_len = ntohl(total_len);
        if (total_len < ZAPI_HEADER_SIZE || total_len > sizeof(buf)) {
            printf("[FAIL] invalid message length %u\n", total_len);
            failed++;
            break;
        }

        /* Read remaining bytes */
        size_t remaining = total_len - 4;
        n = recv(fd, buf + 4, remaining, MSG_WAITALL);
        if (n != (ssize_t)remaining) {
            printf("[FAIL] short read on body (%zd/%zu)\n", n, remaining);
            failed++;
            break;
        }

        zapi_message_t msg;
        if (zapi_parse(buf, total_len, &msg) != 0) {
            printf("[FAIL] cannot parse complete message\n");
            failed++;
            break;
        }

        /* Dispatch to DPA */
        danos_tx_t tx = {0};
        if (danos_tx_begin(&tx, "e2e", NULL) != DANOS_OK) {
            printf("[FAIL] tx_begin failed\n");
            failed++;
            break;
        }

        danos_status_t st = zapi_dispatch(&msg, &tx);
        if (st == DANOS_OK) {
            danos_tx_prepare(&tx);
            danos_tx_validate(&tx);
            danos_tx_commit(&tx);
            msgs_processed++;

            if (msg.header.command == ZEBRA_INTERFACE_ADD) {
                iface_msgs++;
                printf("[OK] processed INTERFACE_ADD\n");
            } else if (msg.header.command == ZEBRA_ROUTE_ADD) {
                route_msgs++;
                printf("[OK] processed ROUTE_ADD\n");
            }
        } else {
            danos_tx_abort(&tx);
            printf("[FAIL] dispatch failed: %s\n", danos_status_str(st));
            failed++;
        }
    }

    close(fd);
    stop_mock_zebra();

    /* Verify we got both messages */
    if (iface_msgs != 1) {
        printf("[FAIL] expected 1 INTERFACE_ADD, got %d\n", iface_msgs);
        failed++;
    }
    if (route_msgs != 1) {
        printf("[FAIL] expected 1 ROUTE_ADD, got %d\n", route_msgs);
        failed++;
    }

    if (failed == 0) {
        printf("\n[C4] E2E: mock_zebra → ZAPI parse → DPA dispatch: PASS\n");
        printf("  messages processed: %d (iface=%d, route=%d)\n",
               msgs_processed, iface_msgs, route_msgs);
        printf("=== fib_e2e_test: ALL PASSED ===\n");
    } else {
        printf("=== fib_e2e_test: FAILURES ===\n");
    }

    return failed;
}
