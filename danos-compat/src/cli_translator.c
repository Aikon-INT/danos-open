/*
 * DANOS-Open Compat: OcNOS CLI Translator Implementation
 *
 * Maps OcNOS-style commands to DANOS DPA equivalents.
 */

#include <danos/compat/cli_translator.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>

static bool g_initialized = false;

/* Command mapping table */
typedef struct {
    const char *ocnos_cmd;
    const char *danos_cmd;
} cli_mapping_t;

static const cli_mapping_t g_mappings[] = {
    /* Interface */
    {"interface ",          "set interface "},
    {"ip address ",         "ipv4 address "},
    {"ipv6 address ",       "ipv6 address "},
    {"no ip address",       "delete ipv4 address"},
    {"no ipv6 address",     "delete ipv6 address"},
    {"shutdown",            "set admin-status down"},
    {"no shutdown",         "set admin-status up"},
    /* VRF */
    {"ip vrf ",             "set vrf "},
    {"vrf forwarding ",     "set vrf-binding "},
    /* BGP */
    {"router bgp ",         "set bgp instance "},
    {"neighbor ",           "set bgp neighbor "},
    {"remote-as ",          "remote-as "},
    {"network ",            "set bgp network "},
    /* OSPF */
    {"router ospf",         "set ospf instance"},
    {"network ",            "set ospf network "},
    /* Static route */
    {"ip route ",           "set static-route "},
    /* ACL */
    {"ip access-list ",     "set acl "},
    {"permit ",             "action permit "},
    {"deny ",               "action deny "},
    /* VLAN */
    {"vlan ",               "set vlan "},
    {"switchport access vlan ", "set access-vlan "},
    /* Save/commit */
    {"write memory",        "commit"},
    {"end",                 "commit"},
};

#define NUM_MAPPINGS (sizeof(g_mappings) / sizeof(g_mappings[0]))

int danos_compat_cli_init(void)
{
    g_initialized = true;
    return 0;
}

int danos_compat_translate_cli(const char *input, char *output, int out_size)
{
    if (!input || !output || out_size <= 0) return -1;
    if (!g_initialized) danos_compat_cli_init();

    /* Try to find a matching prefix */
    for (int i = 0; i < (int)NUM_MAPPINGS; i++) {
        size_t cmd_len = strlen(g_mappings[i].ocnos_cmd);
        if (strncasecmp(input, g_mappings[i].ocnos_cmd, cmd_len) == 0) {
            /* Replace prefix, keep rest */
            const char *rest = input + cmd_len;
            snprintf(output, out_size, "%s%s", g_mappings[i].danos_cmd, rest);
            return 0;
        }
    }

    /* Unknown command: copy as-is with a comment */
    snprintf(output, out_size, "# untranslated: %s", input);
    return -1;
}
