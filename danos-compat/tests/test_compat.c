/*
 * DANOS-Open Compat: Unit Tests
 */

#include <danos/compat/cli_translator.h>
#include <danos/compat/config_import.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

static void test_cli_translate(void)
{
    printf("== CLI Translate ==\n");
    char out[512];

    /* Interface */
    int rc = danos_compat_translate_cli("interface eth0", out, sizeof(out));
    CHECK(rc == 0, "interface translate rc");
    CHECK(strstr(out, "set interface") != NULL, "interface output");

    /* ip address */
    rc = danos_compat_translate_cli("ip address 10.0.0.1/24", out, sizeof(out));
    CHECK(rc == 0, "ip address rc");
    CHECK(strstr(out, "ipv4 address") != NULL, "ip address output");

    /* shutdown */
    rc = danos_compat_translate_cli("shutdown", out, sizeof(out));
    CHECK(rc == 0, "shutdown rc");
    CHECK(strstr(out, "admin-status down") != NULL, "shutdown output");

    /* no shutdown */
    rc = danos_compat_translate_cli("no shutdown", out, sizeof(out));
    CHECK(rc == 0, "no shutdown rc");
    CHECK(strstr(out, "admin-status up") != NULL, "no shutdown output");

    /* BGP */
    rc = danos_compat_translate_cli("router bgp 65001", out, sizeof(out));
    CHECK(rc == 0, "router bgp rc");
    CHECK(strstr(out, "set bgp instance") != NULL, "router bgp output");

    /* Static route */
    rc = danos_compat_translate_cli("ip route 0.0.0.0/0 10.0.0.1", out, sizeof(out));
    CHECK(rc == 0, "ip route rc");
    CHECK(strstr(out, "set static-route") != NULL, "ip route output");

    /* write memory */
    rc = danos_compat_translate_cli("write memory", out, sizeof(out));
    CHECK(rc == 0, "write memory rc");
    CHECK(strcmp(out, "commit") == 0, "write memory output");

    /* Unknown command */
    rc = danos_compat_translate_cli("foobar unknown", out, sizeof(out));
    CHECK(rc == -1, "unknown command rc");
    CHECK(strstr(out, "untranslated") != NULL, "unknown command output");
}

static void test_config_import(void)
{
    printf("== Config Import ==\n");
    const char *tmp = "/tmp/danos_compat_test.cfg";
    FILE *fp = fopen(tmp, "w");
    CHECK(fp != NULL, "create temp config");
    if (!fp) return;
    fprintf(fp, "interface eth0\n");
    fprintf(fp, "ip address 10.0.0.1/24\n");
    fprintf(fp, "no shutdown\n");
    fprintf(fp, "!\n");
    fprintf(fp, "router bgp 65001\n");
    fprintf(fp, "neighbor 10.0.0.2 remote-as 65002\n");
    fclose(fp);

    char out[4096];
    int rc = danos_compat_import_config(tmp, out, sizeof(out));
    CHECK(rc == 0, "import rc");
    CHECK(strstr(out, "danos-config") != NULL, "import json structure");
    CHECK(strstr(out, "set interface") != NULL, "import interface");
    CHECK(strstr(out, "ipv4 address") != NULL, "import ip address");
    CHECK(strstr(out, "admin-status up") != NULL, "import no shutdown");
    CHECK(strstr(out, "set bgp instance") != NULL, "import bgp");

    unlink(tmp);
}

static void test_config_export(void)
{
    printf("== Config Export ==\n");
    const char *tmp = "/tmp/danos_compat_test_out.cfg";
    const char *json = "{\"test\": true}";
    int rc = danos_compat_export_config(json, tmp);
    CHECK(rc == 0, "export rc");

    FILE *fp = fopen(tmp, "r");
    CHECK(fp != NULL, "open exported file");
    if (fp) {
        char buf[256];
        fgets(buf, sizeof(buf), fp);
        CHECK(strstr(buf, "DANOS-Open") != NULL, "export header");
        fclose(fp);
        unlink(tmp);
    }
}

int main(void)
{
    printf("=== DANOS Compat Test Suite ===\n\n");
    danos_compat_cli_init();
    danos_compat_config_init();

    test_cli_translate();
    test_config_import();
    test_config_export();

    printf("\n=== Result: %d passed, %d failed, %d total ===\n",
           g_pass, g_fail, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
