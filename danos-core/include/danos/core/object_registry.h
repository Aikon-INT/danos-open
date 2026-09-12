/*
 * DANOS-Open Core: Object Registry (B1)
 *
 * Generic object store keyed by (type, id). Each object type registers
 * create/read/update/delete hooks. The registry is thread-safe (rwlock).
 */

#ifndef DANOS_CORE_OBJECT_REGISTRY_H__
#define DANOS_CORE_OBJECT_REGISTRY_H__

#include <danos/dpa.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Object store holds opaque blobs keyed by (type, id). */
typedef struct danos_object_entry {
    danos_obj_type_t  type;
    danos_obj_id_t    id;
    void             *data;          /* owned by store, malloc'd */
    size_t            data_size;
    struct danos_object_entry *next;  /* hash chain */
} danos_object_entry_t;

typedef struct {
    danos_object_entry_t **buckets;
    size_t                bucket_count;
    pthread_rwlock_t      lock;
    uint64_t              count;     /* total objects */
} danos_object_store_t;

/* Global default store */
extern danos_object_store_t *g_default_store;

/* Create/destroy store */
danos_object_store_t *danos_object_store_create(size_t bucket_count);
void danos_object_store_destroy(danos_object_store_t *store);

/* CRUD: data is copied into the store. Returns OK/EXISTS/NOT_FOUND. */
danos_status_t danos_object_create(danos_object_store_t *store,
                                   danos_obj_type_t type,
                                   danos_obj_id_t id,
                                   const void *data, size_t size);

danos_status_t danos_object_read(danos_object_store_t *store,
                                 danos_obj_type_t type,
                                 danos_obj_id_t id,
                                 void *out, size_t *out_size);

danos_status_t danos_object_update(danos_object_store_t *store,
                                   danos_obj_type_t type,
                                   danos_obj_id_t id,
                                   const void *data, size_t size);

danos_status_t danos_object_delete(danos_object_store_t *store,
                                   danos_obj_type_t type,
                                   danos_obj_id_t id);

/* Count objects of a given type */
uint64_t danos_object_count(danos_object_store_t *store, danos_obj_type_t type);

/* Iterate all entries (all types) under a read lock. The callback must
 * not modify the store. */
typedef void (*danos_object_iter_cb_t)(danos_object_entry_t *entry, void *user);
void danos_object_iterate(danos_object_store_t *store,
                          danos_object_iter_cb_t cb, void *user);

/* Hash helper */
static inline size_t danos_obj_hash(danos_obj_type_t type, danos_obj_id_t id,
                                    size_t bucket_count)
{
    uint64_t h = (uint64_t)type * 2654435761ULL + id;
    return (size_t)(h % bucket_count);
}

#ifdef __cplusplus
}
#endif

#endif /* DANOS_CORE_OBJECT_REGISTRY_H__ */
