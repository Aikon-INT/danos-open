/*
 * Test: FIB E2E Multi-Message Sequence (C4 enhanced)
 *
 * Tests batch route injection: mock_zebra sends 1 interface + 100 routes,
 * client receives and dispatches all via ZAPI → DPA.
 * Measures throughput (routes/sec) for control plane processing.
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
#include <time.h>

#define MOCK_SOCK "/tmp/danos_mock_zebra_multi.sock"
#define NUM_ROUTES 100

static pid_t g_mock_pid = -1;
static char  g_mock_bin[1024] = "mock_zebra";

static int start_mock_zebra(void)
{
    g_mock_pid = fork();
    if (g_mock_pid < 0) return -1;
    if (g_mock_pid == 0) {
        char routes_arg[16];
        snprintf(routes_arg, sizeof(routes_arg), "%d", NUM_ROUTES);
        execl(g_mock_bin, g_mock_bin, MOCK_SOCK, "--routes", routes_arg, NULL);
        perror("execl");
        _exit(1);
    }
    usleep(200000);
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

    for (int i = 0; i < 10; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return fd;
        }
        usleep(100000);
    }

    close(fd);
    return -1;
}

int main(int argc, char **argv)
{
    int failed = 0;

    /* Resolve mock_zebra path */
    if (argc > 0 && argv[0][0] != '\0') {
        char *slash = strrchr(argv[0], '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - argv[0]) + 1;
            if (dir_len + strlen("mock_zebra") < sizeof(g_mock_bin)) {
                memcpy(g_mock_bin, argv[0], dir_len);
                strcpy(g_mock_bin + dir_len, "mock_zebra");
            }
        }
    }

    if (start_mock_zebra() != 0) {
        printf("[FAIL] cannot start mock_zebra\n");
        return 1;
    }

    int fd = connect_to_mock();
    if (fd < 0) {
        printf("[FAIL] cannot connect to mock_zebra\n");
        stop_mock_zebra();
        return 1;
    }

    printf("[OK] connected to mock_zebra (expecting 1 iface + %d routes)\n",
           NUM_ROUTES);

    /* Receive and process all messages */
    int iface_msgs = 0;
    int route_msgs = 0;
    int total_msgs = 0;

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    int expected_total = 1 + NUM_ROUTES;
    for (int i = 0; i < expected_total; i++) {
        uint8_t buf[4096];

        ssize_t n = recv(fd, buf, 4, MSG_WAITALL);
        if (n != 4) {
            printf("[FAIL] short read on length at msg %d (%zd)\n", i, n);
            failed++;
            break;
        }

        uint32_t total_len;
        memcpy(&total_len, buf, 4);
        total_len = ntohl(total_len);
        if (total_len < ZAPI_HEADER_SIZE || total_len > sizeof(buf)) {
            printf("[FAIL] invalid message length %u at msg %d\n", total_len, i);
            failed++;
            break;
        }

        size_t remaining = total_len - 4;
        n = recv(fd, buf + 4, remaining, MSG_WAITALL);
        if (n != (ssize_t)remaining) {
            printf("[FAIL] short read on body at msg %d\n", i);
            failed++;
            break;
        }

        zapi_message_t msg;
        if (zapi_parse(buf, total_len, &msg) != 0) {
            printf("[FAIL] cannot parse msg %d\n", i);
            failed++;
            break;
        }

        danos_tx_t tx = {0};
        if (danos_tx_begin(&tx, "e2e-multi", NULL) != DANOS_OK) {
            printf("[FAIL] tx_begin at msg %d\n", i);
            failed++;
            break;
        }

        danos_status_t st = zapi_dispatch(&msg, &tx);
        if (st == DANOS_OK) {
            danos_tx_prepare(&tx);
            danos_tx_validate(&tx);
            danos_tx_commit(&tx);
            total_msgs++;

            if (msg.header.command == ZEBRA_INTERFACE_ADD) {
                iface_msgs++;
            } else if (msg.header.command == ZEBRA_ROUTE_ADD) {
                route_msgs++;
            }
        } else {
            danos_tx_abort(&tx);
            printf("[FAIL] dispatch failed at msg %d: %s\n", i, danos_status_str(st));
            failed++;
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0 +
                        (t_end.tv_nsec - t_start.tv_nsec) / 1000000.0;

    close(fd);
    stop_mock_zebra();

    /* Verify counts */
    if (iface_msgs != 1) {
        printf("[FAIL] expected 1 INTERFACE_ADD, got %d\n", iface_msgs);
        failed++;
    }
    if (route_msgs != NUM_ROUTES) {
        printf("[FAIL] expected %d ROUTE_ADD, got %d\n", NUM_ROUTES, route_msgs);
        failed++;
    }

    if (failed == 0) {
        double routes_per_sec = (double)route_msgs / (elapsed_ms / 1000.0);
        printf("\n[C4-Multi] E2E batch route injection: PASS\n");
        printf("  messages: %d total (iface=%d, routes=%d)\n",
               total_msgs, iface_msgs, route_msgs);
        printf("  elapsed: %.2f ms\n", elapsed_ms);
        printf("  throughput: %.0f routes/sec\n", routes_per_sec);
        printf("=== fib_e2e_multi_test: ALL PASSED ===\n");
    } else {
        printf("=== fib_e2e_multi_test: FAILURES ===\n");
    }

    return failed;
}
