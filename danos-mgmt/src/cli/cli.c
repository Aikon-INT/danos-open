/*
 * DANOS-Open Management: CLI Implementation (E1)
 *
 * Implements basic CLI commands that dispatch to DPA operations.
 */

#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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
    /* In production: iterate DPA object store */
    printf("(no interfaces configured)\n");
    return 0;
}

int cli_cmd_show_route(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx; (void)args;
    printf("Routing table:\n");
    printf("%-6s %-40s %-8s %-6s %-10s\n",
           "VRF", "PREFIX", "PROTO", "AD", "NHGROUP");
    /* In production: iterate DPA route store */
    printf("(no routes configured)\n");
    return 0;
}

int cli_cmd_show_vrf(danos_cli_ctx_t *ctx, const char *args)
{
    (void)ctx; (void)args;
    printf("VRF table:\n");
    printf("%-8s %-16s %-8s %-8s\n",
           "VRF_ID", "NAME", "IPv4", "IPv6");
    printf("%-8d %-16s %-8s %-8s\n", 0, "default", "yes", "yes");
    return 0;
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
