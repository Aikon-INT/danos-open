/* Core test runner: aggregates all core tests */
extern int test_object(void);
extern int test_tunnel_crud(void);
extern int test_evpn_crud(void);
extern int test_mroute_crud(void);
extern int test_state(void);
extern int test_transaction(void);
extern int test_event(void);
extern int test_reconciler(void);

#include <stdio.h>

int main(void)
{
    int failed = 0;
    if (test_object()       != 0) failed++;
    if (test_tunnel_crud()  != 0) failed++;
    if (test_evpn_crud()    != 0) failed++;
    if (test_mroute_crud()  != 0) failed++;
    if (test_state()        != 0) failed++;
    if (test_transaction()  != 0) failed++;
    if (test_event()        != 0) failed++;
    if (test_reconciler()   != 0) failed++;
    printf("=== core_test: %s ===\n", failed == 0 ? "ALL PASSED" : "FAILURES");
    return failed;
}
