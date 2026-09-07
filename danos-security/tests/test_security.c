/*
 * DANOS-Open Security: Unit Tests
 */

#include <danos/security/rbac.h>
#include <danos/security/copp.h>
#include <danos/security/audit.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int tests_run = 0;
static int tests_pass = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_pass++; } \
    else { printf("  FAIL: %s\n", msg); } \
} while(0)

static void test_rbac(void)
{
    printf("== RBAC ==\n");
    danos_rbac_init();

    /* admin: all ops on all objects */
    CHECK(danos_rbac_check(DANOS_ROLE_ADMIN, DANOS_SEC_OBJ_ROUTE, DANOS_SEC_OP_DELETE),
          "admin delete route");
    CHECK(danos_rbac_check(DANOS_ROLE_ADMIN, DANOS_SEC_OBJ_SECURITY, DANOS_SEC_OP_UPDATE),
          "admin update security");

    /* viewer: read only */
    CHECK(danos_rbac_check(DANOS_ROLE_VIEWER, DANOS_SEC_OBJ_ROUTE, DANOS_SEC_OP_READ),
          "viewer read route");
    CHECK(!danos_rbac_check(DANOS_ROLE_VIEWER, DANOS_SEC_OBJ_ROUTE, DANOS_SEC_OP_CREATE),
          "viewer cannot create");

    /* operator: read all + write non-security */
    CHECK(danos_rbac_check(DANOS_ROLE_OPERATOR, DANOS_SEC_OBJ_ROUTE, DANOS_SEC_OP_CREATE),
          "operator create route");
    CHECK(!danos_rbac_check(DANOS_ROLE_OPERATOR, DANOS_SEC_OBJ_SECURITY, DANOS_SEC_OP_UPDATE),
          "operator cannot update security");

    /* security-admin: read all + write security */
    CHECK(danos_rbac_check(DANOS_ROLE_SECURITY_ADMIN, DANOS_SEC_OBJ_SECURITY, DANOS_SEC_OP_UPDATE),
          "sec-admin update security");
    CHECK(!danos_rbac_check(DANOS_ROLE_SECURITY_ADMIN, DANOS_SEC_OBJ_ROUTE, DANOS_SEC_OP_DELETE),
          "sec-admin cannot delete route");

    /* role by name */
    danos_sec_role_t role;
    CHECK(danos_rbac_role_by_name("admin", &role) == 0 && role == DANOS_ROLE_ADMIN,
          "role_by_name admin");
    CHECK(danos_rbac_role_by_name("Viewer", &role) == 0 && role == DANOS_ROLE_VIEWER,
          "role_by_name Viewer (case-insensitive)");
    CHECK(danos_rbac_role_by_name("nonexistent", &role) == -1,
          "role_by_name unknown");

    /* role name */
    CHECK(strcmp(danos_rbac_role_name(DANOS_ROLE_ADMIN), "admin") == 0,
          "role_name admin");
}

static void test_copp(void)
{
    printf("== CoPP ==\n");
    danos_copp_init();

    int count;
    const danos_copp_policy_t *defaults = danos_copp_defaults(&count);
    CHECK(defaults != NULL && count == 13, "13 default policies");

    /* Get BGP policy */
    danos_copp_policy_t pol;
    CHECK(danos_copp_get(DANOS_COPP_CLASS_BGP, &pol) == 0, "get BGP policy");
    CHECK(pol.cir_bps == 10000000, "BGP CIR 10Mbps");

    /* Apply custom policy */
    danos_copp_policy_t custom = {
        .cls = DANOS_COPP_CLASS_SSH,
        .cir_bps = 5000000,
        .cb_bytes = 62500,
        .drop_on_exceed = true,
        .exceed_dscp = 0,
    };
    CHECK(danos_copp_apply(&custom) == 0, "apply SSH policy");
    CHECK(danos_copp_get(DANOS_COPP_CLASS_SSH, &pol) == 0 && pol.cir_bps == 5000000,
          "SSH policy updated");
}

static void test_audit(void)
{
    printf("== Audit ==\n");
    /* Use temp path to avoid /var/log permission issues */
    danos_audit_init("/tmp/danos_audit_test.log");

    danos_audit_entry_t entry = {0};
    entry.tx_id = 12345;
    entry.event = DANOS_AUDIT_TX_COMMIT;
    snprintf(entry.initiator, sizeof(entry.initiator), "admin");
    snprintf(entry.source_ip, sizeof(entry.source_ip), "10.0.0.1");
    snprintf(entry.obj_type, sizeof(entry.obj_type), "route");
    snprintf(entry.obj_id, sizeof(entry.obj_id), "10.0.0.0/8");
    snprintf(entry.diff, sizeof(entry.diff), "nh=192.168.1.1");
    clock_gettime(CLOCK_REALTIME, &entry.timestamp);

    CHECK(danos_audit_log(&entry) == 0, "log entry");

    /* Query */
    danos_audit_entry_t results[10];
    int n = danos_audit_query(12345, results, 10);
    CHECK(n == 1, "query by tx_id");
    CHECK(n == 1 && results[0].tx_id == 12345, "queried entry tx_id");

    /* Query all */
    n = danos_audit_query(0, results, 10);
    CHECK(n >= 1, "query all");

    danos_audit_close();
}

int main(void)
{
    printf("=== DANOS Security Test Suite ===\n\n");
    test_rbac();
    test_copp();
    test_audit();
    printf("\n=== Result: %d passed, %d failed, %d total ===\n",
           tests_pass, tests_run - tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
