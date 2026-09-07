/*
 * DANOS-Open Compat: OcNOS CLI Translator Interface
 *
 * Translates OcNOS-like CLI commands to DANOS DPA operations.
 */

#ifndef DANOS_COMPAT_CLI_H__
#define DANOS_COMPAT_CLI_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Translate an OcNOS CLI command string to a DANOS DPA equivalent.
 * input:  OcNOS command (e.g. "interface eth0; ip address 10.0.0.1/24")
 * output: DANOS command (e.g. "set interface eth0 ipv4 address 10.0.0.1/24")
 * out_size: output buffer size
 * Returns 0 on success, -1 on unknown command. */
int danos_compat_translate_cli(const char *input, char *output, int out_size);

/* Initialize the CLI translator with command mapping table. */
int danos_compat_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_COMPAT_CLI_H__ */
