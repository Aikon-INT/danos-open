/*
 * DANOS-Open Security: RBAC Implementation
 *
 * Role-Based Access Control per §20.3.
 * Permission matrix:
 *   admin:          all objects, all ops
 *   operator:       all objects read + non-security write
 *   viewer:         all objects read only
 *   security-admin: all objects read + security write
 */

#include <danos/security/rbac.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

static bool g_initialized = false;

int danos_rbac_init(void)
{
    g_initialized = true;
    return 0;
}

bool danos_rbac_check(danos_sec_role_t role,
                      danos_sec_obj_type_t obj,
                      danos_sec_op_t op)
{
    if (!g_initialized) danos_rbac_init();

    switch (role) {
    case DANOS_ROLE_ADMIN:
        /* admin: all objects, all operations */
        return true;

    case DANOS_ROLE_VIEWER:
        /* viewer: read only */
        return (op == DANOS_SEC_OP_READ);

    case DANOS_ROLE_OPERATOR:
        /* operator: read all + write non-security */
        if (op == DANOS_SEC_OP_READ) return true;
        /* write: allowed for all except security objects */
        return (obj != DANOS_SEC_OBJ_SECURITY);

    case DANOS_ROLE_SECURITY_ADMIN:
        /* security-admin: read all + write security */
        if (op == DANOS_SEC_OP_READ) return true;
        /* write: allowed only for security objects */
        return (obj == DANOS_SEC_OBJ_SECURITY);

    default:
        /* unknown role: deny */
        return false;
    }
}

int danos_rbac_role_by_name(const char *name, danos_sec_role_t *out)
{
    if (!name || !out) return -1;

    if (strcasecmp(name, "admin") == 0) {
        *out = DANOS_ROLE_ADMIN;
        return 0;
    }
    if (strcasecmp(name, "operator") == 0) {
        *out = DANOS_ROLE_OPERATOR;
        return 0;
    }
    if (strcasecmp(name, "viewer") == 0) {
        *out = DANOS_ROLE_VIEWER;
        return 0;
    }
    if (strcasecmp(name, "security-admin") == 0 ||
        strcasecmp(name, "security_admin") == 0) {
        *out = DANOS_ROLE_SECURITY_ADMIN;
        return 0;
    }
    return -1;
}

const char *danos_rbac_role_name(danos_sec_role_t role)
{
    switch (role) {
    case DANOS_ROLE_ADMIN:          return "admin";
    case DANOS_ROLE_OPERATOR:       return "operator";
    case DANOS_ROLE_VIEWER:         return "viewer";
    case DANOS_ROLE_SECURITY_ADMIN: return "security-admin";
    default:                        return "unknown";
    }
}
