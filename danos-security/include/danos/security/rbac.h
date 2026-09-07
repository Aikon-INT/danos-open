/*
 * DANOS-Open Security: RBAC Interface
 *
 * Role-Based Access Control for DPA Transaction validation.
 * Design: v1.1/security/security_architecture.md §20.3
 */

#ifndef DANOS_SECURITY_RBAC_H__
#define DANOS_SECURITY_RBAC_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DPA object types (subset for security) */
typedef enum {
    DANOS_SEC_OBJ_INTERFACE  = 1,
    DANOS_SEC_OBJ_VRF        = 2,
    DANOS_SEC_OBJ_ROUTE      = 3,
    DANOS_SEC_OBJ_ACL        = 4,
    DANOS_SEC_OBJ_QOS        = 5,
    DANOS_SEC_OBJ_BFD        = 6,
    DANOS_SEC_OBJ_SECURITY   = 7,  /* AAA/keys/CoPP */
    DANOS_SEC_OBJ_ALL        = 99,
} danos_sec_obj_type_t;

/* Operations */
typedef enum {
    DANOS_SEC_OP_READ   = 1,
    DANOS_SEC_OP_CREATE = 2,
    DANOS_SEC_OP_UPDATE = 3,
    DANOS_SEC_OP_DELETE = 4,
} danos_sec_op_t;

/* Predefined roles */
typedef enum {
    DANOS_ROLE_ADMIN          = 1,  /* all objects all ops */
    DANOS_ROLE_OPERATOR       = 2,  /* read all + write non-security */
    DANOS_ROLE_VIEWER         = 3,  /* read only */
    DANOS_ROLE_SECURITY_ADMIN = 4,  /* read all + write security */
} danos_sec_role_t;

/* Check if role has permission for operation on object type.
 * Returns true if allowed, false if denied.
 * Role-object-op matrix per §20.3.1. */
bool danos_rbac_check(danos_sec_role_t role,
                      danos_sec_obj_type_t obj,
                      danos_sec_op_t op);

/* Get role by name (case-insensitive).
 * Returns 0 on success, -1 if unknown role. */
int danos_rbac_role_by_name(const char *name, danos_sec_role_t *out);

/* Get role name string. */
const char *danos_rbac_role_name(danos_sec_role_t role);

/* Initialize RBAC module. */
int danos_rbac_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_SECURITY_RBAC_H__ */
