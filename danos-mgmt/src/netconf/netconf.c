/*
 * DANOS-Open Management: NETCONF Server Implementation (E3)
 */

#include "netconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void netconf_init(netconf_ctx_t *ctx, uint16_t port)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->port = port ? port : 830;
}

/* Simple XML tag extraction */
static const char *find_tag(const char *xml, const char *tag)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "<%s", tag);
    return strstr(xml, pattern);
}

netconf_rpc_type_t netconf_parse_rpc(const char *xml)
{
    if (!xml) return NETCONF_RPC_UNKNOWN;

    /* Look for RPC operation tags */
    if (find_tag(xml, "get-config"))     return NETCONF_RPC_GET_CONFIG;
    if (find_tag(xml, "edit-config"))    return NETCONF_RPC_EDIT_CONFIG;
    if (find_tag(xml, "get") && !strstr(xml, "get-config"))
                                         return NETCONF_RPC_GET;
    if (find_tag(xml, "close-session"))  return NETCONF_RPC_CLOSE_SESSION;
    if (find_tag(xml, "kill-session"))   return NETCONF_RPC_KILL_SESSION;
    if (find_tag(xml, "commit"))         return NETCONF_RPC_COMMIT;
    if (find_tag(xml, "discard-changes"))return NETCONF_RPC_DISCARD;
    if (find_tag(xml, "lock"))           return NETCONF_RPC_LOCK;
    if (find_tag(xml, "unlock"))         return NETCONF_RPC_UNLOCK;

    return NETCONF_RPC_UNKNOWN;
}

char *netconf_hello_message(void)
{
    return strdup(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <capabilities>\n"
        "    <capability>urn:ietf:params:netconf:base:1.0</capability>\n"
        "    <capability>urn:ietf:params:netconf:base:1.1</capability>\n"
        "    <capability>urn:ietf:params:netconf:capability:writable-running:1.0</capability>\n"
        "    <capability>urn:ietf:params:netconf:capability:candidate:1.0</capability>\n"
        "    <capability>urn:ietf:params:netconf:capability:rollback-on-error:1.0</capability>\n"
        "    <capability>urn:ietf:params:netconf:capability:validate:1.1</capability>\n"
        "    <capability>urn:danos:yang:1.0</capability>\n"
        "  </capabilities>\n"
        "  <session-id>1</session-id>\n"
        "</hello>\n"
    );
}

static char *ok_reply(void)
{
    return strdup(
        "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <ok/>\n"
        "</rpc-reply>\n"
    );
}

static char *error_reply(const char *msg)
{
    char *resp = malloc(512);
    if (resp) {
        snprintf(resp, 512,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
            "  <rpc-error>\n"
            "    <error-type>application</error-type>\n"
            "    <error-tag>operation-failed</error-tag>\n"
            "    <error-severity>error</error-severity>\n"
            "    <error-message>%s</error-message>\n"
            "  </rpc-error>\n"
            "</rpc-reply>\n",
            msg ? msg : "unknown error");
    }
    return resp;
}

static char *get_config_reply(void)
{
    /* Return current running config (empty for v0.1) */
    return strdup(
        "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <data>\n"
        "    <interfaces xmlns=\"urn:danos:yang:danos-iface\">\n"
        "    </interfaces>\n"
        "    <routes xmlns=\"urn:danos:yang:danos-route\">\n"
        "    </routes>\n"
        "  </data>\n"
        "</rpc-reply>\n"
    );
}

char *netconf_handle_rpc(netconf_ctx_t *ctx, const char *xml)
{
    if (!xml) return error_reply("null input");

    netconf_rpc_type_t rpc_type = netconf_parse_rpc(xml);
    ctx->rpc_count++;

    switch (rpc_type) {
    case NETCONF_RPC_GET_CONFIG:
        return get_config_reply();

    case NETCONF_RPC_EDIT_CONFIG:
        /* In production: parse config and apply via DPA transaction */
        return ok_reply();

    case NETCONF_RPC_GET:
        return get_config_reply();

    case NETCONF_RPC_COMMIT:
        return ok_reply();

    case NETCONF_RPC_DISCARD:
        return ok_reply();

    case NETCONF_RPC_LOCK:
    case NETCONF_RPC_UNLOCK:
        return ok_reply();

    case NETCONF_RPC_CLOSE_SESSION:
        return ok_reply();

    case NETCONF_RPC_UNKNOWN:
    default:
        ctx->error_count++;
        return error_reply("unknown RPC");
    }
}

void netconf_get_stats(netconf_ctx_t *ctx, uint64_t *rpc_count,
                       uint64_t *error_count)
{
    if (rpc_count)   *rpc_count   = ctx->rpc_count;
    if (error_count) *error_count = ctx->error_count;
}
