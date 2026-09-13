/*
 * DANOS-Open Management: Model Path Registry implementation (v0.5)
 */

#include "model_paths.h"
#include <danos/core/object_registry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Coerce JSON_IETF/JSON/ASCII scalar encodings into typed values so
 * that clients sending text (gnmic --encoding json_ietf) work with
 * leaf writes. */
static void coerce_scalar(const gnmi_typed_value_t *in, gnmi_typed_value_t *out)
{
    if (in->kind != GNMI_VAL_JSON && in->kind != GNMI_VAL_JSON_IETF &&
        in->kind != GNMI_VAL_ASCII && in->kind != GNMI_VAL_STRING) {
        *out = *in;
        return;
    }
    const char *s = in->s;
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) {
        out->kind = GNMI_VAL_BOOL; out->b = true;
    } else if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) {
        out->kind = GNMI_VAL_BOOL; out->b = false;
    } else {
        char *end = NULL;
        unsigned long long v = strtoull(s, &end, 10);
        if (end && *end == '\0' && end != s) {
            out->kind = GNMI_VAL_UINT; out->u = v;
        } else {
            *out = *in;  /* leave as-is; field validation rejects */
        }
    }
}

/* Generated matchers (tools/gen-model-paths) are authoritative;
 * the hand-written logic below is the fallback. */
static bool has_config_segment(const gnmi_path_t *p)
{
    for (uint32_t i = 0; i < p->elem_count; i++) {
        if (strcmp(p->elems[i].name, "config") == 0) return true;
    }
    return false;
}

static danos_status_t gnmi_model_gen_resolve(const gnmi_path_t *p,
                                             gnmi_model_binding_t *b)
{
    if (p->elem_count == 0 || p->elem_count > GNMI_MAX_ELEMS)
        return DANOS_ERR_NOT_FOUND;
#include "model_paths_gen.inc"
    return DANOS_ERR_NOT_FOUND;
}

danos_status_t gnmi_model_resolve(const gnmi_path_t *path,
                                  gnmi_model_binding_t *b)
{
    if (!path || path->elem_count == 0) return DANOS_ERR_NOT_FOUND;
    memset(b, 0, sizeof(*b));
    if (gnmi_model_gen_resolve(path, b) == DANOS_OK) return DANOS_OK;
    const char *top = path->elems[0].name;
    const char *second = path->elem_count > 1 ? path->elems[1].name : NULL;
    const char *third  = path->elem_count > 2 ? path->elems[2].name : NULL;
    const char *fourth = path->elem_count > 3 ? path->elems[3].name : NULL;

    if (strcmp(top, "interfaces") == 0) {
        b->obj_type = DANOS_OBJ_IFACE;
        if (path->elem_count == 1) {
            b->kind = GNMI_MODEL_LIST;
            return DANOS_OK;
        }
        if (!second || strcmp(second, "interface") != 0 ||
            !path->elems[1].has_key)
            return DANOS_ERR_NOT_FOUND;

        if (path->elem_count == 2) {
            b->kind = GNMI_MODEL_ENTRY;
            return DANOS_OK;
        }
        if (!third || (strcmp(third, "config") != 0 &&
                       strcmp(third, "state") != 0))
            return DANOS_ERR_NOT_FOUND;
        b->config_tree = strcmp(third, "config") == 0;

        if (path->elem_count == 3) {
            /* /config or /state container: treat as entry */
            b->kind = GNMI_MODEL_ENTRY;
            return DANOS_OK;
        }
        b->kind = GNMI_MODEL_LEAF;
        if (strcmp(fourth, "mtu") == 0)          b->field = GNMI_FIELD_MTU;
        else if (strcmp(fourth, "enabled") == 0) b->field = GNMI_FIELD_ENABLED;
        else if (strcmp(fourth, "name") == 0)    b->field = GNMI_FIELD_NAME;
        else if (strcmp(fourth, "link-up") == 0 && !b->config_tree)
                                                 b->field = GNMI_FIELD_LINK_UP;
        else return DANOS_ERR_NOT_FOUND;
        return DANOS_OK;
    }

    if (strcmp(top, "vrfs") == 0) {
        b->obj_type = DANOS_OBJ_VRF;
        if (path->elem_count == 1) { b->kind = GNMI_MODEL_LIST; return DANOS_OK; }
        if (!second || strcmp(second, "vrf") != 0 || !path->elems[1].has_key)
            return DANOS_ERR_NOT_FOUND;
        b->kind = GNMI_MODEL_ENTRY;
        if (path->elem_count > 2 && fourth) {
            if (strcmp(fourth, "enabled") == 0) {
                b->kind = GNMI_MODEL_LEAF;
                b->field = GNMI_FIELD_ENABLED;
                b->config_tree = strcmp(third, "config") == 0;
                return DANOS_OK;
            }
            return DANOS_ERR_NOT_FOUND;
        }
        return DANOS_OK;
    }

    if (strcmp(top, "routes") == 0 && path->elem_count == 1) {
        b->obj_type = DANOS_OBJ_ROUTE;
        b->kind = GNMI_MODEL_LIST;
        return DANOS_OK;
    }

    return DANOS_ERR_NOT_FOUND;
}

danos_status_t gnmi_model_read_leaf(danos_obj_type_t type, uint64_t key,
                                    gnmi_model_field_t field,
                                    const void *obj, size_t obj_size,
                                    gnmi_typed_value_t *out)
{
    (void)key;
    memset(out, 0, sizeof(*out));
    if (type == DANOS_OBJ_IFACE && obj_size >= sizeof(danos_iface_t)) {
        const danos_iface_t *i = obj;
        switch (field) {
        case GNMI_FIELD_MTU:
            out->kind = GNMI_VAL_UINT; out->u = i->mtu; return DANOS_OK;
        case GNMI_FIELD_ENABLED:
            out->kind = GNMI_VAL_BOOL; out->b = i->admin_up; return DANOS_OK;
        case GNMI_FIELD_NAME:
            out->kind = GNMI_VAL_STRING;
            snprintf(out->s, sizeof(out->s), "%s", i->name);
            return DANOS_OK;
        case GNMI_FIELD_LINK_UP:
            out->kind = GNMI_VAL_BOOL; out->b = i->link_up; return DANOS_OK;
        default:
            return DANOS_ERR_INVALID_ARG;
        }
    }
    if (type == DANOS_OBJ_VRF && obj_size >= sizeof(danos_vrf_t)) {
        const danos_vrf_t *v = obj;
        switch (field) {
        case GNMI_FIELD_ENABLED:
            out->kind = GNMI_VAL_BOOL; out->b = v->ipv4_active; return DANOS_OK;
        default:
            return DANOS_ERR_INVALID_ARG;
        }
    }
    return DANOS_ERR_INVALID_ARG;
}

danos_status_t gnmi_model_apply_leaf(danos_obj_type_t type,
                                     gnmi_model_field_t field,
                                     void *obj, size_t obj_size,
                                     const gnmi_typed_value_t *val_raw)
{
    if (!obj || !val_raw) return DANOS_ERR_INVALID_ARG;
    gnmi_typed_value_t coerced;
    coerce_scalar(val_raw, &coerced);
    const gnmi_typed_value_t *val = &coerced;
    if (type == DANOS_OBJ_IFACE && obj_size >= sizeof(danos_iface_t)) {
        danos_iface_t *i = obj;
        switch (field) {
        case GNMI_FIELD_MTU:
            if (val->kind != GNMI_VAL_UINT && val->kind != GNMI_VAL_INT)
                return DANOS_ERR_INVALID_ARG;
            if (val->u < 68 || val->u > 9216) return DANOS_ERR_INVALID_ARG;
            i->mtu = (uint16_t)val->u;
            return DANOS_OK;
        case GNMI_FIELD_ENABLED:
            if (val->kind != GNMI_VAL_BOOL) return DANOS_ERR_INVALID_ARG;
            i->admin_up = val->b;
            return DANOS_OK;
        case GNMI_FIELD_LINK_UP:
            return DANOS_ERR_INVALID_ARG;  /* oper, read-only */
        default:
            return DANOS_ERR_INVALID_ARG;
        }
    }
    if (type == DANOS_OBJ_VRF && obj_size >= sizeof(danos_vrf_t)) {
        danos_vrf_t *v = obj;
        if (field == GNMI_FIELD_ENABLED && val->kind == GNMI_VAL_BOOL) {
            v->ipv4_active = val->b;
            return DANOS_OK;
        }
        return DANOS_ERR_INVALID_ARG;
    }
    return DANOS_ERR_INVALID_ARG;
}
