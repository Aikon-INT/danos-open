/*
 * DANOS-Open Core: Event Bus (B7)
 *
 * Pub/sub event system. Subscribers register a callback + event mask.
 * Events are dispatched synchronously (for v0.1; async queue in v0.2+).
 */

#ifndef DANOS_CORE_EVENT_BUS_H__
#define DANOS_CORE_EVENT_BUS_H__

#include <danos/dpa.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct danos_event_sub {
    uint64_t                id;
    uint32_t                mask;       /* bitmask of danos_event_type_t */
    danos_event_cb_t        cb;
    void                   *user;
    struct danos_event_sub *next;
} danos_event_sub_t;

typedef struct {
    danos_event_sub_t *subs;
    pthread_rwlock_t  lock;
    uint64_t          next_sub_id;
    uint64_t          events_published;
} danos_event_bus_t;

extern danos_event_bus_t *g_event_bus;

int danos_event_bus_init(void);
void danos_event_bus_fini(void);

/* Internal: publish event (called by core modules) */
void danos_event_publish(const danos_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_CORE_EVENT_BUS_H__ */
