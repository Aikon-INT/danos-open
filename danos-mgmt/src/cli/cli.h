/*
 * DANOS-Open Management: CLI (E1)
 *
 * Basic CLI commands: configure / show interface / show route / show vrf
 * Uses a simple readline-like loop. Commands are parsed and dispatched
 * to DPA operations via transactions.
 */

#ifndef DANOS_CLI_H__
#define DANOS_CLI_H__

#include <danos/dpa.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CLI context */
typedef struct {
    bool in_configure_mode;
    char prompt[128];
} danos_cli_ctx_t;

/* Initialize CLI context */
void danos_cli_init(danos_cli_ctx_t *ctx);

/* Process one command line.
 * Returns 0 on success, 1 on exit, negative on error. */
int danos_cli_process(danos_cli_ctx_t *ctx, const char *line);

/* Run interactive CLI loop (reads from stdin) */
int danos_cli_run(danos_cli_ctx_t *ctx);

/* Command handlers (exposed for testing) */
int cli_cmd_show_interface(danos_cli_ctx_t *ctx, const char *args);
int cli_cmd_show_route(danos_cli_ctx_t *ctx, const char *args);
int cli_cmd_show_vrf(danos_cli_ctx_t *ctx, const char *args);
int cli_cmd_configure(danos_cli_ctx_t *ctx, const char *args);
int cli_cmd_exit(danos_cli_ctx_t *ctx, const char *args);
int cli_cmd_help(danos_cli_ctx_t *ctx, const char *args);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_CLI_H__ */
