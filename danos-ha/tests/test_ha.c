/*
 * DANOS-Open HA: Unit Tests
 */

#include <danos/ha/bfd.h>
#include <danos/ha/vrrp.h>
#include <danos/ha/supervisor.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_pass = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_pass++; } \
    else { printf("  FAIL: %s\n", msg); } \
} while(0)

static void test_bfd(void)
{
    printf("== BFD ==\n");
    danos_bfd_init();

    danos_bfd_session_t sess = {
        .id = 1,
        .multihop = true,
        .desired_tx_ms = 1000,
        .required_rx_ms = 1000,
        .detect_mult = 3,
    };
    snprintf(sess.remote, sizeof(sess.remote), "10.0.0.2");
    snprintf(sess.local, sizeof(sess.local), "10.0.0.1");

    CHECK(danos_bfd_create(&sess) == 0, "create BFD session");
    CHECK(danos_bfd_create(&sess) == -1, "duplicate create fails");

    danos_bfd_session_t out;
    CHECK(danos_bfd_read(1, &out) == 0, "read BFD session");
    CHECK(out.multihop == true, "multihop flag");
    CHECK(out.detect_mult == 3, "detect_mult 3");

    CHECK(danos_bfd_get_state(1) == DANOS_BFD_STATE_DOWN, "initial state DOWN");

    CHECK(danos_bfd_delete(1) == 0, "delete BFD session");
    CHECK(danos_bfd_read(1, &out) == -1, "read after delete fails");
}

static void test_vrrp(void)
{
    printf("== VRRP ==\n");
    danos_vrrp_init();

    danos_vrrp_session_t sess = {
        .vrid = 1,
        .ifindex = 10,
        .priority = 100,
        .preempt = true,
        .advert_int_ms = 1000,
        .bfd_link = true,
        .bfd_session_id = 1,
        .bfd_down_priority = 50,
    };
    snprintf(sess.virtual_ip, sizeof(sess.virtual_ip), "10.0.0.254");

    CHECK(danos_vrrp_create(&sess) == 0, "create VRRP session");
    CHECK(danos_vrrp_get_state(1, 10) == DANOS_VRRP_STATE_INIT, "initial state INIT");

    danos_vrrp_session_t out;
    CHECK(danos_vrrp_read(1, 10, &out) == 0, "read VRRP");
    CHECK(out.priority == 100, "priority 100");
    CHECK(out.bfd_link == true, "BFD link enabled");

    CHECK(danos_vrrp_delete(1, 10) == 0, "delete VRRP");
}

static void test_supervisor(void)
{
    printf("== Supervisor ==\n");
    danos_sup_init();

    danos_sup_proc_t proc = {
        .restart_min_ms = 1000,
        .restart_max_ms = 60000,
        .critical = true,
    };
    snprintf(proc.name, sizeof(proc.name), "zebra");
    snprintf(proc.cmdline, sizeof(proc.cmdline), "/usr/lib/frr/zebra");
    snprintf(proc.pidfile, sizeof(proc.pidfile), "/run/frr/zebra.pid");

    CHECK(danos_sup_register(&proc) == 0, "register zebra");
    CHECK(danos_sup_get_state("zebra") == DANOS_SUP_STATE_STOPPED, "initial STOPPED");

    CHECK(danos_sup_start() == 0, "start supervision");
    CHECK(danos_sup_get_state("zebra") == DANOS_SUP_STATE_RUNNING, "state RUNNING");

    CHECK(danos_sup_stop() == 0, "stop supervision");
    CHECK(danos_sup_get_state("zebra") == DANOS_SUP_STATE_STOPPED, "state STOPPED after stop");

    CHECK(danos_sup_unregister("zebra") == 0, "unregister zebra");
}

int main(void)
{
    printf("=== DANOS HA Test Suite ===\n\n");
    test_bfd();
    test_vrrp();
    test_supervisor();
    printf("\n=== Result: %d passed, %d failed, %d total ===\n",
           tests_pass, tests_run - tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
