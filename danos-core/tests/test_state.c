/* Test: State Store (B2) */
#include <danos/core/state_store.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int diff_count;

static int diff_cb(danos_obj_type_t type, danos_obj_id_t id, void *user)
{
    (void)type; (void)id; (void)user;
    diff_count++;
    return 0;
}

int test_state(void)
{
    danos_state_store_t *ss = danos_state_store_create();
    assert(ss != NULL);

    /* Set desired, no programmed → diff should be 1 */
    danos_status_t st = danos_state_set(ss, DANOS_STATE_DESIRED,
                                        DANOS_OBJ_ROUTE, 1, "route1", 7);
    assert(st == DANOS_OK);

    diff_count = 0;
    uint64_t diffs = danos_state_diff_desired_programmed(ss, diff_cb, NULL);
    assert(diffs == 1);
    assert(diff_count == 1);

    /* Set programmed to match → diff should be 0 */
    st = danos_state_set(ss, DANOS_STATE_PROGRAMMED,
                         DANOS_OBJ_ROUTE, 1, "route1", 7);
    assert(st == DANOS_OK);
    diffs = danos_state_diff_desired_programmed(ss, diff_cb, NULL);
    assert(diffs == 0);

    /* Change desired → diff should be 1 again */
    st = danos_state_set(ss, DANOS_STATE_DESIRED,
                         DANOS_OBJ_ROUTE, 1, "route2", 7);
    diffs = danos_state_diff_desired_programmed(ss, diff_cb, NULL);
    assert(diffs == 1);

    danos_state_store_destroy(ss);
    printf("[PASS] test_state: 4-state model + diff works\n");
    return 0;
}
