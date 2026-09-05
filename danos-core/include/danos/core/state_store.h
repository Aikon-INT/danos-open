/*
 * DANOS-Open Core: State Model (B2)
 *
 * CONFIG → DESIRED → PROGRAMMED → OPER
 *
 * Each object can exist in multiple states simultaneously:
 *   - DESIRED:   what management/control plane wants
 *   - PROGRAMMED: what was committed to backend
 *   - OPER:      what backend reports as actual
 *
 * Reconciler compares DESIRED vs PROGRAMMED/OPER and repairs.
 */

#ifndef DANOS_CORE_STATE_STORE_H__
#define DANOS_CORE_STATE_STORE_H__

#include <danos/core/object_registry.h>
#include <danos/dpa.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DANOS_STATE_NONE       = 0,
    DANOS_STATE_CONFIG     = 1,  /* user config (persisted) */
    DANOS_STATE_DESIRED    = 2,  /* normalized desired */
    DANOS_STATE_PROGRAMMED = 3,  /* committed to backend */
    DANOS_STATE_OPER       = 4,  /* backend operational reality */
} danos_state_kind_t;

/* A state store holds three object stores: desired, programmed, oper */
typedef struct {
    danos_object_store_t *desired;
    danos_object_store_t *programmed;
    danos_object_store_t *oper;
} danos_state_store_t;

danos_state_store_t *danos_state_store_create(void);
void danos_state_store_destroy(danos_state_store_t *ss);

/* Set/get object in a specific state */
danos_status_t danos_state_set(danos_state_store_t *ss,
                               danos_state_kind_t kind,
                               danos_obj_type_t type, danos_obj_id_t id,
                               const void *data, size_t size);

danos_status_t danos_state_get(danos_state_store_t *ss,
                               danos_state_kind_t kind,
                               danos_obj_type_t type, danos_obj_id_t id,
                               void *out, size_t *out_size);

danos_status_t danos_state_delete(danos_state_store_t *ss,
                                  danos_state_kind_t kind,
                                  danos_obj_type_t type, danos_obj_id_t id);

/* Compute diff: objects in desired but not in programmed (or different) */
typedef int (*danos_state_diff_cb_t)(danos_obj_type_t type, danos_obj_id_t id,
                                     void *user);

uint64_t danos_state_diff_desired_programmed(danos_state_store_t *ss,
                                             danos_state_diff_cb_t cb,
                                             void *user);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_CORE_STATE_STORE_H__ */
