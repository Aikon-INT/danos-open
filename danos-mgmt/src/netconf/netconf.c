/*
 * DANOS-Open Management: NETCONF Server Implementation (E3)
 */

#include "netconf.h"
#include "../gnmi/model_paths.h"
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
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

/* ---- model-layer wired config view + edit ------------------------------- */

char g_nc_body[8192];
int g_nc_off = 0;

static void nc_iface_xml_iter(danos_object_entry_t *e, void *user)
{
    (void)user;
    if (e->type != DANOS_OBJ_IFACE || e->data_size < sizeof(danos_iface_t))
        return;
    const danos_iface_t *i = e->data;
    g_nc_off += snprintf(g_nc_body + g_nc_off, sizeof(g_nc_body) - g_nc_off,
        "      <interface>\n"
        "        <name>%s</name>\n"
        "        <mtu>%u</mtu>\n"
        "        <enabled>%s</enabled>\n"
        "      </interface>\n",
        i->name, i->mtu, i->admin_up ? "true" : "false");
}

static char *get_config_reply(void)
{
    g_nc_off = snprintf(g_nc_body, sizeof(g_nc_body),
        "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\n"
        "  <data>\n"
        "    <interfaces xmlns=\"urn:danos:yang:danos-iface\">\n");
    danos_object_iterate(g_default_store, nc_iface_xml_iter, NULL);
    g_nc_off += snprintf(g_nc_body + g_nc_off, sizeof(g_nc_body) - g_nc_off,
        "    </interfaces>\n"
        "  </data>\n"
        "</rpc-reply>\n");
    return strdup(g_nc_body);
}

static void extract_text(const char *xml, const char *tag,
                         char *out, size_t cap)
{
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *p = strstr(xml, open);
    if (!p) return;
    p += strlen(open);
    const char *e = strstr(p, close);
    if (!e) return;
    size_t n = (size_t)(e - p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

struct nc_lookup { const char *want; danos_iface_t *out; bool found; };

static void nc_lookup_iface_iter(danos_object_entry_t *e, void *user)
{
    struct nc_lookup *lu = user;
    if (lu->found) return;
    if (e->type != DANOS_OBJ_IFACE || e->data_size < sizeof(danos_iface_t))
        return;
    const danos_iface_t *i = e->data;
    if (strcmp(i->name, lu->want) == 0) {
        memcpy(lu->out, i, sizeof(*i));
        lu->found = true;
    }
}

static char *edit_config_apply(const char *xml)
{
    const char *iblock = find_tag(xml, "interface>");
    if (!iblock) return ok_reply();

    char name[64] = {0}, mtu[32] = {0}, enabled[16] = {0};
    extract_text(iblock, "name", name, sizeof(name));
    extract_text(iblock, "mtu", mtu, sizeof(mtu));
    extract_text(iblock, "enabled", enabled, sizeof(enabled));
    if (!name[0]) return error_reply("interface name missing");

    danos_iface_t target;
    struct nc_lookup lu = { .want = name, .out = &target, .found = false };
    danos_object_iterate(g_default_store, nc_lookup_iface_iter, &lu);
    if (!lu.found) return error_reply("interface not found");

    if (mtu[0]) {
        gnmi_typed_value_t v = { .kind = GNMI_VAL_JSON_IETF };
        snprintf(v.s, sizeof(v.s), "%s", mtu);
        danos_status_t st = gnmi_model_apply_leaf(DANOS_OBJ_IFACE,
                                                  GNMI_FIELD_MTU,
                                                  &target, sizeof(target), &v);
        if (st != DANOS_OK) return error_reply(danos_status_str(st));
    }
    if (enabled[0]) {
        gnmi_typed_value_t v = { .kind = GNMI_VAL_JSON_IETF };
        snprintf(v.s, sizeof(v.s), "%s", enabled);
        danos_status_t st = gnmi_model_apply_leaf(DANOS_OBJ_IFACE,
                                                  GNMI_FIELD_ENABLED,
                                                  &target, sizeof(target), &v);
        if (st != DANOS_OK) return error_reply(danos_status_str(st));
    }
    danos_status_t st = danos_object_update(g_default_store,
                                            DANOS_OBJ_IFACE, target.ifindex,
                                            &target, sizeof(target));
    if (st != DANOS_OK) return error_reply(danos_status_str(st));
    return ok_reply();
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
        return edit_config_apply(xml);

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
