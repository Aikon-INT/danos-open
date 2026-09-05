/*
 * DANOS-Open Core: Capability Registry
 * Aggregates capabilities from all registered backends.
 */

#include <danos/dpa.h>
#include <string.h>

/* Uses danos-dpa's backend registry; this file provides core-level
 * capability validation for transactions. */

/* Check if any backend supports a given object type */
bool danos_core_capability_supported(danos_obj_type_t type)
{
    for (uint32_t i = 0; ; i++) {
        danos_backend_info_t be;
        if (danos_backend_get_info(i, &be) != DANOS_OK) break;
        for (uint32_t c = 0; c < be.cap_count; c++) {
            if (be.caps[c].type == type && be.caps[c].supported)
                return true;
        }
    }
    return false;
}

/* Check if a specific backend supports an object type with enough capacity */
danos_status_t danos_core_capability_check(const char *backend_name,
                                           danos_obj_type_t type,
                                           uint32_t required_count)
{
    danos_capability_t cap;
    danos_status_t st = danos_capability_query(backend_name, type, &cap);
    if (st != DANOS_OK) return st;
    if (!cap.supported) return DANOS_ERR_NOT_SUPPORTED;
    if (cap.max_count != 0 && required_count > cap.max_count)
        return DANOS_ERR_NO_CAPACITY;
    return DANOS_OK;
}
