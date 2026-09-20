/*
 * DANOS-Open Management: Model Path Registry (v0.5, P1)
 *
 * First real binding between gNMI paths and YANG models. The table
 * below mirrors the openconfig-interfaces@2.4.1 subtree (and our
 * danos vrf/route extensions); leaf bindings give gNMI Get/Set the
 * authoritative mapping from model paths to DPA object fields.
 *
 * Supported shapes:
 *   /interfaces                                   -> LIST (all ifaces)
 *   /interfaces/interface[name=X]                 -> ENTRY (one iface)
 *   /interfaces/interface[name=X]/config/mtu      -> LEAF (uint16)
 *   /interfaces/interface[name=X]/config/enabled  -> LEAF (bool)
 *   /interfaces/interface[name=X]/config/name     -> LEAF (string)
 *   /interfaces/interface[name=X]/state/...       -> same leaves, oper
 *   /vrfs, /vrfs/vrf[id=N]                        -> LIST/ENTRY
 *   /routes                                       -> LIST
 *
 * Unresolvable paths return DANOS_ERR_NOT_FOUND so gNMI answers with
 * gRPC error instead of silently ignoring the path.
 */

#ifndef DANOS_MODEL_PATHS_H__
#define DANOS_MODEL_PATHS_H__

#include <danos/dpa.h>
#include "gnmi_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GNMI_MODEL_LIST = 1,    /* whole list: emit one update per object */
    GNMI_MODEL_ENTRY,       /* one object (key present) */
    GNMI_MODEL_LEAF,        /* one field of one object */
} gnmi_model_kind_t;

typedef enum {
    GNMI_FIELD_NONE = 0,
    GNMI_FIELD_MTU,
    GNMI_FIELD_ENABLED,
    GNMI_FIELD_NAME,
    GNMI_FIELD_LINK_UP,     /* oper only, read-only */
    GNMI_FIELD_IPV4_ADDRESS,
    GNMI_FIELD_IPV6_ADDRESS,
} gnmi_model_field_t;

typedef struct {
    gnmi_model_kind_t   kind;
    danos_obj_type_t    obj_type;
    gnmi_model_field_t  field;
    bool                config_tree;   /* /config vs /state */
} gnmi_model_binding_t;

/* Resolve a gNMI path. Returns DANOS_OK and fills `b`, or
 * DANOS_ERR_NOT_FOUND for paths outside the model. */
danos_status_t gnmi_model_resolve(const gnmi_path_t *path,
                                  gnmi_model_binding_t *b);

/* Supported model list (capabilities source of truth, v0.13) */


void gnmi_model_supported_models(const gnmi_model_data_t **models,
                                 uint32_t *count);

/* Read a leaf value out of an object blob. Returns DANOS_OK on success
 * (value in `out`), DANOS_ERR_NOT_FOUND for missing key objects. */
danos_status_t gnmi_model_read_leaf(danos_obj_type_t type, uint64_t key,
                                    gnmi_model_field_t field,
                                    const void *obj, size_t obj_size,
                                    gnmi_typed_value_t *out);

/* Apply a leaf value to an object blob in place. Returns DANOS_OK, or
 * DANOS_ERR_INVALID_ARG for type mismatches / read-only fields. */
danos_status_t gnmi_model_apply_leaf(danos_obj_type_t type,
                                     gnmi_model_field_t field,
                                     void *obj, size_t obj_size,
                                     const gnmi_typed_value_t *val);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_MODEL_PATHS_H__ */
