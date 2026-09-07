/*
 * DANOS-Open Compat: OcNOS Config Import/Export Interface
 *
 * Import OcNOS configuration format and convert to DPA YANG model,
 * and export DPA configuration to OcNOS format.
 */

#ifndef DANOS_COMPAT_CONFIG_H__
#define DANOS_COMPAT_CONFIG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Import OcNOS config file, convert to DANOS YANG JSON.
 * input_path:  OcNOS config file path
 * output:      YANG JSON output buffer
 * out_size:    output buffer size
 * Returns 0 on success, negative on error. */
int danos_compat_import_config(const char *input_path, char *output, int out_size);

/* Export DANOS YANG JSON to OcNOS config format.
 * input_json:  YANG JSON string
 * output_path: output file path
 * Returns 0 on success. */
int danos_compat_export_config(const char *input_json, const char *output_path);

/* Initialize config import/export with semantic mapping. */
int danos_compat_config_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_COMPAT_CONFIG_H__ */
