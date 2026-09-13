/*
 * DANOS-Open Management: CLI Implementation (E1)
 *
 * Implements basic CLI commands that dispatch to DPA operations.
 */

#include "cli.h"
#include "../gnmi/model_paths.h"
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

struct cli_lookup { const char *want; danos_iface_t *out; bool found; };
static void cli_show_iface_iter(danos_object_entry_t *e, void *user);
static void cli_show_route_iter(danos_object_entry_t *e, void *user);
static void cli_show_vrf_iter(danos_object_entry_t *e, void *user);
static void cli_lookup_iface_iter(danos_object_entry_t *e, void *user);

void danos_cli_init(danos_cli_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->in_configure_mode = false;
    strcpy(ctx->prompt, "danos> ");
}

/* Trim leading/trailing whitespace */
static void trim(char *s)
{
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
}

/* Split command line into command and args */
static void split_cmd(const char *line, char *cmd, size_t cmd_size,
                      char *args, size_t args_size)
{
    cmd[0] = args[0] = '\0';
    const char *p = line;
    while (isspace((unsigned char)*p)) p++;
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && i < cmd_size - 1) {
        cmd[i++] = *p++;
    }
    cmd[i] = '\0';
    while (isspace((unsigned char)*p)) p++;
    size_t args_len = strlen(p);
    if (args_len >= args_size) args_len = args_size - 1;
    memcpy(args, p, args_len);
    args[args_len] = '\0';
    trim(args);
}

/* =========================================================================
 * Command handlers
 * ========================================================================= */

int cli_cmd_help(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx; (void)args;
    printf("Available commands:\n");
    printf("  configure               Enter configuration mode\n");
    printf("  show interface          Show all interfaces\n");
    printf("  show route              Show routing table\n");
    printf("  show vrf                Show VRF table\n");
    printf("  set interface <name> mtu <N>       (configure mode)\n");
    printf("  set interface <name> [no] shutdown (configure mode)\n");
    printf("  exit                    Exit current mode / quit\n");
    printf("  help                    Show this help\n");
    return 0;
}

int cli_cmd_show_interface(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx; (void)args;
    printf("Interface table:\n");
    printf("%-8s %-16s %-8s %-6s %-8s\n",
           "IFINDEX", "NAME", "MTU", "ADMIN", "OPER");
    int rows = 0;
    danos_object_iterate(g_default_store, cli_show_iface_iter, &rows);
    if (rows == 0) printf("(no interfaces configured)\n");
    return 0;
}

static void cli_show_iface_iter(danos_object_entry_t *e, void *user)
{
    if (e->type != DANOS_OBJ_IFACE || e->data_size < sizeof(danos_iface_t))
        return;
    const danos_iface_t *i = e->data;
    (*(int *)user)++;
    printf("%-8u %-16s %-8u %-6s %-8s\n",
           i->ifindex, i->name, i->mtu,
           i->admin_up ? "up" : "down", i->link_up ? "up" : "down");
}

int cli_cmd_show_route(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx; (void)args;
    printf("Routing table:\n");
    printf("%-6s %-40s %-8s %-6s %-10s\n",
           "VRF", "PREFIX", "PROTO", "AD", "NHGROUP");
    int rows = 0;
    danos_object_iterate(g_default_store, cli_show_route_iter, &rows);
    if (rows == 0) printf("(no routes configured)\n");
    return 0;
}

static void cli_show_route_iter(danos_object_entry_t *e, void *user)
{
    if (e->type != DANOS_OBJ_ROUTE || e->data_size < sizeof(danos_route_t))
        return;
    const danos_route_t *r = e->data;
    (*(int *)user)++;
    char pfx[64];
    if (r->prefix.addr.af == DANOS_AF_IPV4) {
        snprintf(pfx, sizeof(pfx), "%u.%u.%u.%u/%u",
                 r->prefix.addr.addr[0], r->prefix.addr.addr[1],
                 r->prefix.addr.addr[2], r->prefix.addr.addr[3],
                 r->prefix.prefix_len);
    } else {
        snprintf(pfx, sizeof(pfx), "<ipv6>/%u", r->prefix.prefix_len);
    }
    printf("%-6u %-40s %-8u %-6u %-10lu\n",
           r->vrf_id, pfx, r->protocol, r->admin_distance,
           (unsigned long)r->nhgroup_id);
}

int cli_cmd_show_vrf(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx; (void)args;
    printf("VRF table:\n");
    printf("%-8s %-16s %-8s %-8s\n",
           "VRF_ID", "NAME", "IPv4", "IPv6");
    printf("%-8d %-16s %-8s %-8s\n", 0, "default", "yes", "yes");
    int rows = 0;
    danos_object_iterate(g_default_store, cli_show_vrf_iter, &rows);
    return 0;
}

static void cli_show_vrf_iter(danos_object_entry_t *e, void *user)
{
    if (e->type != DANOS_OBJ_VRF || e->data_size < sizeof(danos_vrf_t))
        return;
    const danos_vrf_t *v = e->data;
    (*(int *)user)++;
    printf("%-8u %-16s %-8s %-8s\n", v->vrf_id, v->name,
           v->ipv4_active ? "yes" : "no", v->ipv6_active ? "yes" : "no");
}

/* configure-mode: set interface <name> mtu <N> | [no] shutdown.
 * Mutations go through the model registry so CLI behaves identically
 * to gNMI/NETCONF leaf writes (validation, persistence, metrics). */
static int cli_cmd_set(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx;
    char what[64] = {0}, name[64] = {0}, rest[768] = {0};
    split_cmd(args, what, sizeof(what), rest, sizeof(rest));
    if (strcmp(what, "interface") != 0) {
        printf("%% unsupported set target: %s\n", what);
        return -1;
    }
    char attr[64], value[512];
    char rest2[768], rest3[768];
    snprintf(rest2, sizeof(rest2), "%s", rest);
    split_cmd(rest2, name, sizeof(name), rest3, sizeof(rest3));
    split_cmd(rest3, attr, sizeof(attr), value, sizeof(value));

    /* name-based lookup via store iteration */
    danos_iface_t target;
    danos_object_store_t *st = g_default_store;
    struct cli_lookup { const char *want; danos_iface_t *out; bool found; } lu
        = { .want = name, .out = &target, .found = false };
    danos_object_iterate(st, cli_lookup_iface_iter, &lu);
    if (!lu.found) {
        printf("%% interface %s not found\n", name);
        return -1;
    }

    gnmi_model_field_t field;
    gnmi_typed_value_t val;
    memset(&val, 0, sizeof(val));
    bool no_form = strcmp(attr, "no") == 0;
    if (no_form) split_cmd(value, attr, sizeof(attr), value, sizeof(value));

    if (strcmp(attr, "mtu") == 0) {
        field = GNMI_FIELD_MTU;
        val.kind = GNMI_VAL_UINT;
        val.u = strtoul(value, NULL, 10);
    } else if (strcmp(attr, "shutdown") == 0) {
        field = GNMI_FIELD_ENABLED;
        val.kind = GNMI_VAL_BOOL;
        val.b = no_form;   /* "shutdown" -> disabled, "no shutdown" -> up */
    } else {
        printf("%% unsupported attribute: %s\n", attr);
        return -1;
    }

    danos_status_t ast = gnmi_model_apply_leaf(DANOS_OBJ_IFACE, field,
                                               &target, sizeof(target), &val);
    if (ast != DANOS_OK) {
        printf("%% validation failed: %s\n", danos_status_str(ast));
        return -1;
    }
    ast = danos_object_update(st, DANOS_OBJ_IFACE, target.ifindex,
                              &target, sizeof(target));
    if (ast != DANOS_OK) {
        printf("%% store update failed: %s\n", danos_status_str(ast));
        return -1;
    }
    return 0;
}

static void cli_lookup_iface_iter(danos_object_entry_t *e, void *user)
{
    struct cli_lookup *lu = user;
    if (lu->found) return;
    if (e->type != DANOS_OBJ_IFACE || e->data_size < sizeof(danos_iface_t))
        return;
    const danos_iface_t *i = e->data;
    if (strcmp(i->name, lu->want) == 0) {
        memcpy(lu->out, i, sizeof(*i));
        lu->found = true;
    }
}

int cli_cmd_configure(danos_cli_ctx_t *ctx, const char *args)
{
    (void)args;
    ctx->in_configure_mode = true;
    strcpy(ctx->prompt, "danos(config)# ");
    printf("Entering configuration mode\n");
    return 0;
}

int cli_cmd_exit(danos_cli_ctx_t *ctx, const char *args)
{
    (void)args;
    if (ctx->in_configure_mode) {
        ctx->in_configure_mode = false;
        strcpy(ctx->prompt, "danos> ");
        printf("Exiting configuration mode\n");
        return 0;
    }
    return 1;  /* exit CLI */
}

/* =========================================================================
 * Command dispatch
 * ========================================================================= */

int danos_cli_process(danos_cli_ctx_t *ctx, const char *line)
{
    if (!ctx || !line) return -1;

    char buf[1024];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim(buf);

    if (buf[0] == '\0') return 0;  /* empty line */

    char cmd[64], args[960];
    split_cmd(buf, cmd, sizeof(cmd), args, sizeof(args));

    /* Handle "show" subcommands */
    if (strcmp(cmd, "show") == 0) {
        char subcmd[64], subargs[896];
        split_cmd(args, subcmd, sizeof(subcmd), subargs, sizeof(subargs));
        if (strcmp(subcmd, "interface") == 0 || strcmp(subcmd, "int") == 0) {
            return cli_cmd_show_interface(ctx, subargs);
        } else if (strcmp(subcmd, "route") == 0 || strcmp(subcmd, "ip") == 0) {
            return cli_cmd_show_route(ctx, subargs);
        } else if (strcmp(subcmd, "vrf") == 0) {
            return cli_cmd_show_vrf(ctx, subargs);
        } else {
            printf("Unknown show command: %s\n", subcmd);
            return -1;
        }
    }

    if (ctx->in_configure_mode && strcmp(cmd, "set") == 0) {
        return cli_cmd_set(ctx, args);
    }
    if (strcmp(cmd, "configure") == 0 || strcmp(cmd, "conf") == 0) {
        return cli_cmd_configure(ctx, args);
    }
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        return cli_cmd_exit(ctx, args);
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        return cli_cmd_help(ctx, args);
    }

    printf("Unknown command: %s (type 'help' for available commands)\n", cmd);
    return -1;
}

int danos_cli_run(danos_cli_ctx_t *ctx)
{
    char line[1024];
    while (1) {
        printf("%s", ctx->prompt);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        /* Remove trailing newline */
        line[strcspn(line, "\n")] = '\0';
        int rc = danos_cli_process(ctx, line);
        if (rc == 1) break;  /* exit */
    }
    return 0;
}
