/* Test: Event Bus (B7) */
#include <danos/core/event_bus.h>
#include <stdio.h>
#include <assert.h>

static int event_count;
static void on_event(const danos_event_t *ev, void *user)
{
    (void)ev; (void)user;
    event_count++;
}

int test_event(void)
{
    danos_event_bus_init();
    event_count = 0;

    /* Subscribe to all events */
    uint64_t sub_id = danos_event_subscribe(
        (danos_event_type_t)0xFFFF, on_event, NULL);
    assert(sub_id > 0);

    /* Publish events */
    danos_event_t ev = {0};
    ev.type = DANOS_EVENT_OBJ_CREATED;
    danos_event_publish(&ev);
    ev.type = DANOS_EVENT_OBJ_UPDATED;
    danos_event_publish(&ev);
    ev.type = DANOS_EVENT_TX_COMMITTED;
    danos_event_publish(&ev);

    assert(event_count == 3);

    /* Unsubscribe */
    danos_event_unsubscribe(sub_id);
    danos_event_publish(&ev);
    assert(event_count == 3);  /* no new events received */

    danos_event_bus_fini();
    printf("[PASS] test_event: pub/sub works\n");
    return 0;
}
