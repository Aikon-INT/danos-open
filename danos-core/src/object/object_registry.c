/*
 * DANOS-Open Core: Object Registry implementation (B1)
 */

#include <danos/core/object_registry.h>
#include <danos/core/persist.h>
#include <danos/core/event_bus.h>
#include <danos/core/backend_ops.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define DEFAULT_BUCKET_COUNT 1024

danos_object_store_t *g_default_store = NULL;

danos_object_store_t *danos_object_store_create(size_t bucket_count)
{
    if (bucket_count == 0) bucket_count = DEFAULT_BUCKET_COUNT;

    danos_object_store_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->buckets = calloc(bucket_count, sizeof(danos_object_entry_t *));
    if (!s->buckets) { free(s); return NULL; }

    s->bucket_count = bucket_count;
    pthread_rwlock_init(&s->lock, NULL);
    s->count = 0;
    return s;
}

void danos_object_store_destroy(danos_object_store_t *s)
{
    if (!s) return;
    pthread_rwlock_wrlock(&s->lock);
    for (size_t i = 0; i < s->bucket_count; i++) {
        danos_object_entry_t *e = s->buckets[i];
        while (e) {
            danos_object_entry_t *next = e->next;
            free(e->data);
            free(e);
            e = next;
        }
    }
    free(s->buckets);
    pthread_rwlock_unlock(&s->lock);
    pthread_rwlock_destroy(&s->lock);
    free(s);
}

static danos_object_entry_t *find_entry(danos_object_store_t *s,
                                        danos_obj_type_t type,
                                        danos_obj_id_t id)
{
    size_t h = danos_obj_hash(type, id, s->bucket_count);
    danos_object_entry_t *e = s->buckets[h];
    while (e) {
        if (e->type == type && e->id == id) return e;
        e = e->next;
    }
    return NULL;
}

danos_status_t danos_object_create(danos_object_store_t *s,
                                   danos_obj_type_t type,
                                   danos_obj_id_t id,
                                   const void *data, size_t size)
{
    if (!s || !data || size == 0) return DANOS_ERR_INVALID_ARG;

    pthread_rwlock_wrlock(&s->lock);
    if (find_entry(s, type, id)) {
        pthread_rwlock_unlock(&s->lock);
        return DANOS_ERR_EXISTS;
    }

    danos_object_entry_t *e = calloc(1, sizeof(*e));
    if (!e) { pthread_rwlock_unlock(&s->lock); return DANOS_ERR_NO_MEMORY; }
    e->data = malloc(size);
    if (!e->data) { free(e); pthread_rwlock_unlock(&s->lock); return DANOS_ERR_NO_MEMORY; }

    e->type = type;
    e->id = id;
    e->data_size = size;
    memcpy(e->data, data, size);

    size_t h = danos_obj_hash(type, id, s->bucket_count);
    e->next = s->buckets[h];
    s->buckets[h] = e;
    s->count++;

    pthread_rwlock_unlock(&s->lock);
    danos_persist_on_mutation(s, 1 /* WAL_OP_CREATE */, (int)type, id, data, size);

    danos_programming_mark_dirty();
    if (g_event_bus) {
        danos_event_t ev = {0};
        ev.type = DANOS_EVENT_OBJ_CREATED;
        ev.obj_type = type;
        ev.obj_id = id;
        ev.timestamp_ns = (uint64_t)time(NULL) * 1000000000ULL;
        danos_event_publish(&ev);
    }
    return DANOS_OK;
}

danos_status_t danos_object_read(danos_object_store_t *s,
                                 danos_obj_type_t type,
                                 danos_obj_id_t id,
                                 void *out, size_t *out_size)
{
    if (!s || !out_size) return DANOS_ERR_INVALID_ARG;

    pthread_rwlock_rdlock(&s->lock);
    danos_object_entry_t *e = find_entry(s, type, id);
    if (!e) { pthread_rwlock_unlock(&s->lock); return DANOS_ERR_NOT_FOUND; }

    /* If out is NULL, just return the required size */
    if (out == NULL) {
        *out_size = e->data_size;
        pthread_rwlock_unlock(&s->lock);
        return DANOS_ERR_INVALID_ARG;  /* signals: exists, need bigger buffer */
    }

    if (*out_size < e->data_size) {
        *out_size = e->data_size;
        pthread_rwlock_unlock(&s->lock);
        return DANOS_ERR_INVALID_ARG;  /* buffer too small */
    }
    memcpy(out, e->data, e->data_size);
    *out_size = e->data_size;
    pthread_rwlock_unlock(&s->lock);
    return DANOS_OK;
}

danos_status_t danos_object_update(danos_object_store_t *s,
                                   danos_obj_type_t type,
                                   danos_obj_id_t id,
                                   const void *data, size_t size)
{
    if (!s || !data || size == 0) return DANOS_ERR_INVALID_ARG;

    pthread_rwlock_wrlock(&s->lock);
    danos_object_entry_t *e = find_entry(s, type, id);
    if (!e) { pthread_rwlock_unlock(&s->lock); return DANOS_ERR_NOT_FOUND; }

    void *new_data = malloc(size);
    if (!new_data) { pthread_rwlock_unlock(&s->lock); return DANOS_ERR_NO_MEMORY; }
    memcpy(new_data, data, size);

    free(e->data);
    e->data = new_data;
    e->data_size = size;
    pthread_rwlock_unlock(&s->lock);
    danos_persist_on_mutation(s, 2 /* WAL_OP_UPDATE */, (int)type, id, data, size);

    danos_programming_mark_dirty();
    if (g_event_bus) {
        danos_event_t ev = {0};
        ev.type = DANOS_EVENT_OBJ_UPDATED;
        ev.obj_type = type;
        ev.obj_id = id;
        ev.timestamp_ns = (uint64_t)time(NULL) * 1000000000ULL;
        danos_event_publish(&ev);
    }
    return DANOS_OK;
}

danos_status_t danos_object_delete(danos_object_store_t *s,
                                   danos_obj_type_t type,
                                   danos_obj_id_t id)
{
    if (!s) return DANOS_ERR_INVALID_ARG;

    pthread_rwlock_wrlock(&s->lock);
    size_t h = danos_obj_hash(type, id, s->bucket_count);
    danos_object_entry_t *prev = NULL;
    danos_object_entry_t *e = s->buckets[h];
    while (e) {
        if (e->type == type && e->id == id) {
            if (prev) prev->next = e->next;
            else      s->buckets[h] = e->next;
            free(e->data);
            free(e);
            s->count--;
            pthread_rwlock_unlock(&s->lock);
            danos_persist_on_mutation(s, 3 /* WAL_OP_DELETE */, (int)type, id,
                                      NULL, 0);
            danos_programming_mark_dirty();
            if (g_event_bus) {
                danos_event_t ev = {0};
                ev.type = DANOS_EVENT_OBJ_DELETED;
                ev.obj_type = type;
                ev.obj_id = id;
                ev.timestamp_ns = (uint64_t)time(NULL) * 1000000000ULL;
                danos_event_publish(&ev);
            }
            return DANOS_OK;
        }
        prev = e;
        e = e->next;
    }
    pthread_rwlock_unlock(&s->lock);
    return DANOS_ERR_NOT_FOUND;
}

uint64_t danos_object_count(danos_object_store_t *s, danos_obj_type_t type)
{
    if (!s) return 0;
    uint64_t cnt = 0;
    pthread_rwlock_rdlock(&s->lock);
    for (size_t i = 0; i < s->bucket_count; i++) {
        danos_object_entry_t *e = s->buckets[i];
        while (e) {
            if (e->type == type) cnt++;
            e = e->next;
        }
    }
    pthread_rwlock_unlock(&s->lock);
    return cnt;
}

void danos_object_iterate(danos_object_store_t *store,
                          danos_object_iter_cb_t cb, void *user)
{
    if (!store || !cb) return;
    pthread_rwlock_rdlock(&store->lock);
    for (size_t i = 0; i < store->bucket_count; i++) {
        for (danos_object_entry_t *e = store->buckets[i]; e; e = e->next) {
            cb(e, user);
        }
    }
    pthread_rwlock_unlock(&store->lock);
}
