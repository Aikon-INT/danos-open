/*
 * DPA API Conformance Test Suite (A6)
 *
 * Every backend must pass these tests. They verify the DPA API contract
 * independent of any specific backend implementation.
 *
 * A backend under test provides:
 *   - backend_init() / backend_fini()
 *   - The standard DPA C ABI functions
 *
 * This file contains the test runner. Individual test cases are in
 * conformance_*.c files.
 */

#include <danos/dpa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test case prototype */
typedef struct {
    const char *name;
    int (*fn)(void);
} conformance_test_t;

/* External test functions (one per conformance area) */
extern int conf_tx_lifecycle(void);
extern int conf_tx_concurrent(void);
extern int conf_iface_crud(void);
extern int conf_vrf_crud(void);
extern int conf_route_crud(void);
extern int conf_nh_crud(void);
extern int conf_nhgroup_crud(void);
extern int conf_acl_crud(void);
extern int conf_qos_crud(void);
extern int conf_capability_query(void);
extern int conf_version_negotiate(void);

static const conformance_test_t kTests[] = {
    {"tx_lifecycle",        conf_tx_lifecycle},
    {"tx_concurrent",       conf_tx_concurrent},
    {"iface_crud",          conf_iface_crud},
    {"vrf_crud",            conf_vrf_crud},
    {"route_crud",          conf_route_crud},
    {"nh_crud",             conf_nh_crud},
    {"nhgroup_crud",        conf_nhgroup_crud},
    {"acl_crud",            conf_acl_crud},
    {"qos_crud",            conf_qos_crud},
    {"capability_query",    conf_capability_query},
    {"version_negotiate",   conf_version_negotiate},
};

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int passed = 0, failed = 0;
    size_t total = sizeof(kTests) / sizeof(kTests[0]);

    printf("=== DPA Conformance Suite: %zu tests ===\n", total);
    for (size_t i = 0; i < total; i++) {
        printf("[RUN ] %s ... ", kTests[i].name);
        fflush(stdout);
        int rc = kTests[i].fn();
        if (rc == 0) {
            printf("PASS\n");
            passed++;
        } else {
            printf("FAIL (rc=%d)\n", rc);
            failed++;
        }
    }
    printf("=== Result: %d passed, %d failed, %zu total ===\n",
           passed, failed, total);
    return failed == 0 ? 0 : 1;
}
