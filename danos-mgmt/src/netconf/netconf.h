/*
 * DANOS-Open Management: NETCONF Server Skeleton (E3)
 *
 * Implements NETCONF protocol message parsing and dispatch.
 * NETCONF runs over SSH (RFC 6242), but this skeleton implements
 * the protocol layer (XML message parsing, rpc dispatch, get-config,
 * edit-config) without requiring libssh.
 */

#ifndef DANOS_NETCONF_H__
#define DANOS_NETCONF_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NETCONF context */
typedef struct {
    bool running;
    uint16_t port;          /* SSH port (default 830) */
    uint64_t rpc_count;
    uint64_t error_count;
} netconf_ctx_t;

/* NETCONF RPC types */
typedef enum {
    NETCONF_RPC_GET_CONFIG    = 1,
    NETCONF_RPC_EDIT_CONFIG   = 2,
    NETCONF_RPC_GET           = 3,
    NETCONF_RPC_GET_OPERATIONS= 4,
    NETCONF_RPC_CLOSE_SESSION = 5,
    NETCONF_RPC_KILL_SESSION  = 6,
    NETCONF_RPC_COMMIT        = 7,
    NETCONF_RPC_DISCARD       = 8,
    NETCONF_RPC_LOCK          = 9,
    NETCONF_RPC_UNLOCK        = 10,
    NETCONF_RPC_UNKNOWN       = 0,
} netconf_rpc_type_t;

/* Initialize NETCONF server */
void netconf_init(netconf_ctx_t *ctx, uint16_t port);

/* Parse a NETCONF RPC XML message and identify the RPC type.
 * Returns RPC type, or NETCONF_RPC_UNKNOWN on parse failure. */
netconf_rpc_type_t netconf_parse_rpc(const char *xml);

/* Handle a NETCONF RPC.
 * Returns XML response string (caller must free).
 * Returns NULL on error. */
char *netconf_handle_rpc(netconf_ctx_t *ctx, const char *xml);

/* Generate NETCONF <hello> message (capabilities advertisement) */
char *netconf_hello_message(void);

/* Get statistics */
void netconf_get_stats(netconf_ctx_t *ctx, uint64_t *rpc_count,
                       uint64_t *error_count);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_NETCONF_H__ */
