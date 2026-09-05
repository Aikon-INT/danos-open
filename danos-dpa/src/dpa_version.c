/*
 * DANOS-Open DPA version negotiation
 * Implements danos_dpa_get_version() and backend registry.
 */

#include <danos/dpa.h>
#include <string.h>

/* Current DPA API version: v0.1 */
#define DANOS_DPA_VERSION_MAJOR 0
#define DANOS_DPA_VERSION_MINOR 1
#define DANOS_DPA_VERSION_PATCH 0

danos_version_t danos_dpa_get_version(void)
{
    danos_version_t v;
    v.major   = DANOS_DPA_VERSION_MAJOR;
    v.minor   = DANOS_DPA_VERSION_MINOR;
    v.patch   = DANOS_DPA_VERSION_PATCH;
    v.reserved = 0;
    return v;
}

/* -----------------------------------------------------------------------
 * Backend registry
 * Backends register themselves at startup. Core queries via index.
 * ----------------------------------------------------------------------- */

#define DANOS_MAX_BACKENDS 8

typedef struct {
    danos_backend_info_t info;
    int in_use;
} backend_slot_t;

static backend_slot_t g_backends[DANOS_MAX_BACKENDS];
static int g_backend_count = 0;

/* Register a backend. Called by backend init code. */
danos_status_t danos_backend_register(const danos_backend_info_t *info)
{
    if (info == NULL) return DANOS_ERR_INVALID_ARG;
    if (g_backend_count >= DANOS_MAX_BACKENDS) return DANOS_ERR_NO_CAPACITY;

    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i].in_use &&
            strncmp(g_backends[i].info.name, info->name,
                    sizeof(g_backends[i].info.name)) == 0) {
            return DANOS_ERR_EXISTS;
        }
    }

    g_backends[g_backend_count].info = *info;
    g_backends[g_backend_count].in_use = 1;
    g_backend_count++;
    return DANOS_OK;
}

/* Unregister a backend by name. */
danos_status_t danos_backend_unregister(const char *name)
{
    if (name == NULL) return DANOS_ERR_INVALID_ARG;
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i].in_use &&
            strncmp(g_backends[i].info.name, name,
                    sizeof(g_backends[i].info.name)) == 0) {
            g_backends[i].in_use = 0;
            return DANOS_OK;
        }
    }
    return DANOS_ERR_NOT_FOUND;
}

/* Iterate backends. Returns NOT_FOUND when index out of range. */
danos_status_t danos_backend_get_info(uint32_t index, danos_backend_info_t *out)
{
    if (out == NULL) return DANOS_ERR_INVALID_ARG;
    if (index >= (uint32_t)g_backend_count) return DANOS_ERR_NOT_FOUND;
    if (!g_backends[index].in_use) return DANOS_ERR_NOT_FOUND;
    *out = g_backends[index].info;
    return DANOS_OK;
}

/* Reset backend registry (for testing). */
void danos_backend_reset(void)
{
    g_backend_count = 0;
    memset(g_backends, 0, sizeof(g_backends));
}
