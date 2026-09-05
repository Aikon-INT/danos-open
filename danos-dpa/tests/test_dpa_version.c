/*
 * Test: DPA version negotiation (A4)
 */
#include <danos/dpa.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void danos_backend_reset(void);
extern danos_status_t danos_backend_register(const danos_backend_info_t *info);

static int test_version(void)
{
    danos_version_t v = danos_dpa_get_version();
    assert(v.major == 0);
    assert(v.minor == 1);
    assert(v.patch == 0);
    printf("[PASS] test_version: DPA API v%u.%u.%u\n", v.major, v.minor, v.patch);
    return 0;
}

static int test_backend_register(void)
{
    danos_backend_reset();

    /* Register a mock VPP backend */
    static const char *features[] = {"ipv4", "ipv6", "multipath"};
    static const danos_capability_t caps[] = {
        {DANOS_OBJ_ROUTE, true, 1000000, 3, features, NULL},
        {DANOS_OBJ_IFACE, true, 1024,   0, NULL,     NULL},
        {DANOS_OBJ_VRF,   true, 4096,   0, NULL,     NULL},
    };
    danos_backend_info_t be = {0};
    strncpy(be.name, "vpp", sizeof(be.name) - 1);
    be.api_version = danos_dpa_get_version();
    be.cap_count = 3;
    be.caps = caps;

    danos_status_t st = danos_backend_register(&be);
    assert(st == DANOS_OK);

    /* Duplicate register fails */
    st = danos_backend_register(&be);
    assert(st == DANOS_ERR_EXISTS);

    /* Query by index */
    danos_backend_info_t out;
    st = danos_backend_get_info(0, &out);
    assert(st == DANOS_OK);
    assert(strcmp(out.name, "vpp") == 0);

    /* Out of range */
    st = danos_backend_get_info(1, &out);
    assert(st == DANOS_ERR_NOT_FOUND);

    printf("[PASS] test_backend_register: register/query works\n");
    return 0;
}

static int (*runners[])(void) = {
    test_version,
    test_backend_register,
};

int main(void)
{
    for (size_t i = 0; i < sizeof(runners)/sizeof(runners[0]); i++) {
        if (runners[i]() != 0) return 1;
    }
    printf("=== test_dpa_version: all passed ===\n");
    return 0;
}
