/*
 * DANOS-Open Compat: Config Import/Export Implementation
 *
 * Converts OcNOS config format to/from DANOS YANG JSON.
 * Simplified: line-by-line translation using CLI translator.
 */

#include <danos/compat/config_import.h>
#include <danos/compat/cli_translator.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static bool g_initialized = false;

int danos_compat_config_init(void)
{
    danos_compat_cli_init();
    g_initialized = true;
    return 0;
}

int danos_compat_import_config(const char *input_path, char *output, int out_size)
{
    if (!input_path || !output || out_size <= 0) return -1;
    if (!g_initialized) danos_compat_config_init();

    FILE *fp = fopen(input_path, "r");
    if (!fp) return -1;

    int offset = 0;
    offset += snprintf(output + offset, out_size - offset, "{\n  \"danos-config\": {\n");

    char line[512];
    int line_num = 0;
    while (fgets(line, sizeof(line), fp) && offset < out_size - 256) {
        line_num++;
        /* Strip newline */
        line[strcspn(line, "\r\n")] = 0;
        /* Skip empty lines and comments */
        if (line[0] == 0 || line[0] == '!' || line[0] == '#') continue;

        /* Translate each line */
        char translated[512];
        if (danos_compat_translate_cli(line, translated, sizeof(translated)) == 0) {
            offset += snprintf(output + offset, out_size - offset,
                                "    \"%s\": \"%s\",\n", "line", translated);
        }
    }
    fclose(fp);

    /* Remove trailing comma and close JSON */
    if (offset > 0 && output[offset-1] == '\n') offset--;
    if (offset > 0 && output[offset-1] == ',') offset--;
    offset += snprintf(output + offset, out_size - offset, "\n  }\n}\n");

    return 0;
}

int danos_compat_export_config(const char *input_json, const char *output_path)
{
    if (!input_json || !output_path) return -1;
    if (!g_initialized) danos_compat_config_init();

    FILE *fp = fopen(output_path, "w");
    if (!fp) return -1;

    /* Simplified: write JSON as OcNOS-style comment block */
    fprintf(fp, "! DANOS-Open config export (OcNOS-compatible format)\n");
    fprintf(fp, "! Source: DPA YANG JSON\n\n");

    /* In production: parse JSON and emit OcNOS commands */
    fprintf(fp, "! %s\n", input_json);

    fclose(fp);
    return 0;
}
