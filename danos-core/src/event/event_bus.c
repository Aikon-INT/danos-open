/*
 * DANOS-Open Core: Event Bus implementation (B7)
 */

#include <danos/core/event_bus.h>
#include <stdlib.h>
#include <string.h>

danos_event_bus_t *g_event_bus = NULL;

int danos_event_bus_init(void)
{
    if (g_event_bus) return 0;
    g_event_bus = calloc(1, sizeof(*g_event_bus));
    if (!g_event_bus) return -1;
    pthread_rwlock_init(&g_event_bus->lock, NULL);
    g_event_bus->subs = NULL;
    g_event_bus->next_sub_id = 1;
    g_event_bus->events_published = 0;
    return 0;
}

void danos_event_bus_fini(void)
{
    if (!g_event_bus) return;
    pthread_rwlock_wrlock(&g_event_bus->lock);
    danos_event_sub_t *s = g_event_bus->subs;
    while (s) {
        danos_event_sub_t *next = s->next;
        free(s);
        s = next;
    }
    pthread_rwlock_unlock(&g_event_bus->lock);
    pthread_rwlock_destroy(&g_event_bus->lock);
    free(g_event_bus);
    g_event_bus = NULL;
}

uint64_t danos_event_subscribe(danos_event_type_t mask,
                               danos_event_cb_t cb,
                               void *user)
{
    if (!cb) return 0;
    if (!g_event_bus) danos_event_bus_init();

    danos_event_sub_t *sub = calloc(1, sizeof(*sub));
    if (!sub) return 0;
    sub->mask = (uint32_t)mask;
    sub->cb = cb;
    sub->user = user;

    pthread_rwlock_wrlock(&g_event_bus->lock);
    sub->id = g_event_bus->next_sub_id++;
    sub->next = g_event_bus->subs;
    g_event_bus->subs = sub;
    pthread_rwlock_unlock(&g_event_bus->lock);
    return sub->id;
}

void danos_event_unsubscribe(uint64_t sub_id)
{
    if (!g_event_bus || sub_id == 0) return;
    pthread_rwlock_wrlock(&g_event_bus->lock);
    danos_event_sub_t *prev = NULL;
    danos_event_sub_t *s = g_event_bus->subs;
    while (s) {
        if (s->id == sub_id) {
            if (prev) prev->next = s->next;
            else      g_event_bus->subs = s->next;
            free(s);
            break;
        }
        prev = s;
        s = s->next;
    }
    pthread_rwlock_unlock(&g_event_bus->lock);
}

void danos_event_publish(const danos_event_t *event)
{
    if (!g_event_bus || !event) return;
    __atomic_add_fetch(&g_event_bus->events_published, 1, __ATOMIC_SEQ_CST);

    pthread_rwlock_rdlock(&g_event_bus->lock);
    danos_event_sub_t *s = g_event_bus->subs;
    while (s) {
        if (s->mask & (1u << (uint32_t)event->type)) {
            s->cb(event, s->user);
        }
        s = s->next;
    }
    pthread_rwlock_unlock(&g_event_bus->lock);
}
