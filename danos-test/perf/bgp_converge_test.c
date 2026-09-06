/*
 * Test: H1 BGP Convergence Simulation
 *
 * Simulates BGP convergence by injecting 1000 routes via ZAPI → DPA
 * and measuring the time from first route to last route processed.
 *
 * DoD acceptance: 1K routes < 5s
 *
 * This tests the control plane processing path:
 *   ZAPI parse → DPA transaction → state store → commit
 *
 * In production, BGP routes come from FRR BGP daemon via Zebra.
 * Here we use mock_zebra with --routes 1000 to simulate.
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

#define MOCK_SOCK "/tmp/danos_bgp_converge.sock"
#define NUM_ROUTES 1000
#define CONVERGE_TARGET_SEC 5.0

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

    /* Resolve mock_zebra path: search common build locations */
    if (argc > 0 && argv[0][0] != '\0') {
        char *slash = strrchr(argv[0], '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - argv[0]) + 1;
            /* Try relative path: ../danos-fib/tests/mock_zebra */
            snprintf(g_mock_bin, sizeof(g_mock_bin),
                     "%.*s../danos-fib/tests/mock_zebra", (int)dir_len, argv[0]);
            if (access(g_mock_bin, X_OK) != 0) {
                /* Fallback: same directory */
                snprintf(g_mock_bin, sizeof(g_mock_bin),
                         "%.*smock_zebra", (int)dir_len, argv[0]);
            }
        }
    }

    printf("H1: BGP convergence simulation (%d routes, target < %.0fs)\n",
           NUM_ROUTES, CONVERGE_TARGET_SEC);

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

    /* Process all messages: 1 interface + NUM_ROUTES routes */
    int route_msgs = 0;
    int expected_total = 1 + NUM_ROUTES;

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int i = 0; i < expected_total; i++) {
        uint8_t buf[4096];

        ssize_t n = recv(fd, buf, 4, MSG_WAITALL);
        if (n != 4) break;

        uint32_t total_len;
        memcpy(&total_len, buf, 4);
        total_len = ntohl(total_len);
        if (total_len < ZAPI_HEADER_SIZE || total_len > sizeof(buf)) break;

        size_t remaining = total_len - 4;
        n = recv(fd, buf + 4, remaining, MSG_WAITALL);
        if (n != (ssize_t)remaining) break;

        zapi_message_t msg;
        if (zapi_parse(buf, total_len, &msg) != 0) {
            printf("[FAIL] parse error at msg %d\n", i);
            failed++;
            break;
        }

        danos_tx_t tx = {0};
        if (danos_tx_begin(&tx, "bgp-converge", NULL) != DANOS_OK) {
            failed++;
            break;
        }

        danos_status_t st = zapi_dispatch(&msg, &tx);
        if (st == DANOS_OK) {
            danos_tx_prepare(&tx);
            danos_tx_validate(&tx);
            danos_tx_commit(&tx);
            if (msg.header.command == ZEBRA_ROUTE_ADD) {
                route_msgs++;
            }
        } else {
            danos_tx_abort(&tx);
            failed++;
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_sec = (t_end.tv_sec - t_start.tv_sec) +
                         (t_end.tv_nsec - t_start.tv_nsec) / 1000000000.0;

    close(fd);
    stop_mock_zebra();

    /* Verify */
    if (route_msgs != NUM_ROUTES) {
        printf("[FAIL] expected %d routes, got %d\n", NUM_ROUTES, route_msgs);
        failed++;
    }

    double routes_per_sec = (double)route_msgs / elapsed_sec;

    printf("\n=== H1 BGP Convergence Results ===\n");
    printf("  Routes injected: %d\n", route_msgs);
    printf("  Convergence time: %.3f s\n", elapsed_sec);
    printf("  Processing rate: %.0f routes/sec\n", routes_per_sec);
    printf("  Target: < %.0f s\n", CONVERGE_TARGET_SEC);

    if (elapsed_sec < CONVERGE_TARGET_SEC) {
        printf("  [PASS] convergence time within target\n");
    } else {
        printf("  [FAIL] convergence time exceeds target\n");
        failed++;
    }

    if (failed == 0) {
        printf("=== bgp_converge_test: ALL PASSED ===\n");
    } else {
        printf("=== bgp_converge_test: FAILURES ===\n");
    }

    return failed;
}
