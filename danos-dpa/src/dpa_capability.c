/*
 * DANOS-Open DPA capability query
 * Implements danos_capability_query() by searching registered backends.
 */

#include <danos/dpa.h>
#include <string.h>

/* Declared in dpa_version.c */
extern danos_status_t danos_backend_get_info(uint32_t index, danos_backend_info_t *out);

danos_status_t danos_capability_query(const char *backend_name,
                                      danos_obj_type_t type,
                                      danos_capability_t *out)
{
    if (out == NULL) return DANOS_ERR_INVALID_ARG;

    /* Iterate all registered backends */
    for (uint32_t i = 0; ; i++) {
        danos_backend_info_t be;
        danos_status_t st = danos_backend_get_info(i, &be);
        if (st == DANOS_ERR_NOT_FOUND) break;
        if (st != DANOS_OK) return st;

        /* If backend_name specified, skip non-matching */
        if (backend_name != NULL && backend_name[0] != '\0') {
            if (strncmp(be.name, backend_name, sizeof(be.name)) != 0)
                continue;
        }

        /* Search this backend's capabilities for the requested type */
        for (uint32_t c = 0; c < be.cap_count; c++) {
            const danos_capability_t *cap = &be.caps[c];
            if (cap->type == type) {
                *out = *cap;
                return DANOS_OK;
            }
        }

        /* If backend_name specified but capability not found, return NOT_SUPPORTED */
        if (backend_name != NULL && backend_name[0] != '\0') {
            out->type = type;
            out->supported = false;
            out->max_count = 0;
            out->features_count = 0;
            out->features = NULL;
            out->constraints_json = NULL;
            return DANOS_ERR_NOT_SUPPORTED;
        }
    }

    /* No backend found with this capability */
    out->type = type;
    out->supported = false;
    out->max_count = 0;
    out->features_count = 0;
    out->features = NULL;
    out->constraints_json = NULL;
    return DANOS_ERR_NOT_SUPPORTED;
}
