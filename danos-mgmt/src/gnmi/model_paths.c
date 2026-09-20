/*
 * DANOS-Open Management: Model Path Registry implementation (v0.5)
 */

#include "model_paths.h"
#include <danos/core/object_registry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

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
        else if (strcmp(fourth, "ipv4-address") == 0 && b->config_tree)
                                                 b->field = GNMI_FIELD_IPV4_ADDRESS;
        else if (strcmp(fourth, "ipv6-address") == 0 && b->config_tree)
                                                 b->field = GNMI_FIELD_IPV6_ADDRESS;
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

    if (strcmp(top, "routes") == 0) {
        b->obj_type = DANOS_OBJ_ROUTE;
        if (path->elem_count == 1) {
            b->kind = GNMI_MODEL_LIST;
            return DANOS_OK;
        }
        /* composite entry: /routes/route[prefix=X] (v0.12) */
        if (path->elem_count == 2 && second &&
            strcmp(second, "route") == 0 && path->elems[1].has_key) {
            b->kind = GNMI_MODEL_ENTRY;
            return DANOS_OK;
        }
        return DANOS_ERR_NOT_FOUND;
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
        case GNMI_FIELD_IPV4_ADDRESS:
            if (i->ipv4_address.addr.af != DANOS_AF_IPV4) return DANOS_ERR_NOT_FOUND;
            out->kind = GNMI_VAL_STRING;
            inet_ntop(AF_INET, i->ipv4_address.addr.addr, out->s, sizeof(out->s));
            { size_t n = strlen(out->s); snprintf(out->s + n, sizeof(out->s) - n,
                                                   "/%u", i->ipv4_address.prefix_len); }
            return DANOS_OK;
        case GNMI_FIELD_IPV6_ADDRESS:
            if (i->ipv6_address.addr.af != DANOS_AF_IPV6) return DANOS_ERR_NOT_FOUND;
            out->kind = GNMI_VAL_STRING;
            inet_ntop(AF_INET6, i->ipv6_address.addr.addr, out->s, sizeof(out->s));
            { size_t n = strlen(out->s); snprintf(out->s + n, sizeof(out->s) - n,
                                                   "/%u", i->ipv6_address.prefix_len); }
            return DANOS_OK;
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
    memset(&coerced, 0, sizeof(coerced));
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
        case GNMI_FIELD_IPV4_ADDRESS:
        case GNMI_FIELD_IPV6_ADDRESS: {
            if (val->kind != GNMI_VAL_STRING && val->kind != GNMI_VAL_ASCII &&
                val->kind != GNMI_VAL_JSON && val->kind != GNMI_VAL_JSON_IETF)
                return DANOS_ERR_INVALID_ARG;
            char buf[80];
            if (strlen(val->s) >= sizeof(buf)) return DANOS_ERR_INVALID_ARG;
            strcpy(buf, val->s);
            char *slash = strchr(buf, '/');
            if (!slash) return DANOS_ERR_INVALID_ARG;
            *slash++ = '\0';
            char *end = NULL;
            unsigned long plen = strtoul(slash, &end, 10);
            if (!end || *end != '\0') return DANOS_ERR_INVALID_ARG;
            danos_ip_prefix_t *dst = field == GNMI_FIELD_IPV4_ADDRESS
                                   ? &i->ipv4_address : &i->ipv6_address;
            int af = field == GNMI_FIELD_IPV4_ADDRESS ? AF_INET : AF_INET6;
            uint8_t max = field == GNMI_FIELD_IPV4_ADDRESS ? 32 : 128;
            if (plen > max || inet_pton(af, buf, dst->addr.addr) != 1)
                return DANOS_ERR_INVALID_ARG;
            dst->addr.af = field == GNMI_FIELD_IPV4_ADDRESS ? DANOS_AF_IPV4 : DANOS_AF_IPV6;
            dst->prefix_len = (uint8_t)plen;
            return DANOS_OK;
        }
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

/* ---- capabilities consistency (v0.13) ------------------------------------
 * The advertised model list lives HERE, next to the path bindings, so
 * Capabilities can never drift from what the registry implements.
 */
static const gnmi_model_data_t k_supported_models[] = {
    { "openconfig-interfaces",        "OpenConfig",  "2.4.1"  },
    { "openconfig-network-instance",  "OpenConfig",  "0.16.2" },
    { "danos-dpa",                    "DANOS-Open",  "0.3"    },
};

void gnmi_model_supported_models(const gnmi_model_data_t **models,
                                 uint32_t *count)
{
    *models = k_supported_models;
    *count = sizeof(k_supported_models) / sizeof(k_supported_models[0]);
}
