/*
 * DANOS-Open Security: CoPP Implementation
 *
 * Control Plane Policing default policies per §20.6.1.
 * In production, danos_copp_apply() programs VPP classify + policer.
 */

#include <danos/security/copp.h>
#include <string.h>

static bool g_initialized = false;

/* Default CoPP policies (§20.6.1).
 * Rates chosen to allow normal protocol operation while
 * preventing CPU exhaustion from traffic storms. */
static const danos_copp_policy_t g_defaults[] = {
    /* class,              cir_bps,    cb_bytes, drop,  exceed_dscp */
    { DANOS_COPP_CLASS_BGP,     10000000,  125000, true,  0 }, /* 10 Mbps */
    { DANOS_COPP_CLASS_OSPF,     5000000,   62500, true,  0 }, /*  5 Mbps */
    { DANOS_COPP_CLASS_ISIS,     5000000,   62500, true,  0 }, /*  5 Mbps */
    { DANOS_COPP_CLASS_BFD,      5000000,   62500, true,  0 }, /*  5 Mbps */
    { DANOS_COPP_CLASS_SSH,      2000000,   25000, true,  0 }, /*  2 Mbps */
    { DANOS_COPP_CLASS_NETCONF,  2000000,   25000, true,  0 }, /*  2 Mbps */
    { DANOS_COPP_CLASS_GNMI,     2000000,   25000, true,  0 }, /*  2 Mbps */
    { DANOS_COPP_CLASS_ARP,      1000000,   12500, true,  0 }, /*  1 Mbps */
    { DANOS_COPP_CLASS_ICMP,     1000000,   12500, false, 6 }, /*  1 Mbps, mark */
    { DANOS_COPP_CLASS_DHCP,     1000000,   12500, true,  0 }, /*  1 Mbps */
    { DANOS_COPP_CLASS_NTP,      1000000,   12500, true,  0 }, /*  1 Mbps */
    { DANOS_COPP_CLASS_DNS,      1000000,   12500, true,  0 }, /*  1 Mbps */
    { DANOS_COPP_CLASS_EXCEPTION,5000000,   62500, true,  0 }, /*  5 Mbps catch-all */
};

#define NUM_DEFAULTS (sizeof(g_defaults) / sizeof(g_defaults[0]))

/* Applied policies (copy of defaults after init) */
static danos_copp_policy_t g_applied[NUM_DEFAULTS];

int danos_copp_init(void)
{
    memcpy(g_applied, g_defaults, sizeof(g_defaults));
    g_initialized = true;
    return 0;
}

const danos_copp_policy_t *danos_copp_defaults(int *count)
{
    if (count) *count = (int)NUM_DEFAULTS;
    return g_defaults;
}

int danos_copp_apply(const danos_copp_policy_t *policy)
{
    if (!policy) return -1;
    if (!g_initialized) danos_copp_init();

    /* Find and update applied policy for this class */
    for (int i = 0; i < (int)NUM_DEFAULTS; i++) {
        if (g_applied[i].cls == policy->cls) {
            g_applied[i] = *policy;
            /* In production: program VPP classify table + policer here */
            return 0;
        }
    }
    return -1; /* class not found */
}

int danos_copp_get(danos_copp_class_t cls, danos_copp_policy_t *out)
{
    if (!out) return -1;
    if (!g_initialized) danos_copp_init();

    for (int i = 0; i < (int)NUM_DEFAULTS; i++) {
        if (g_applied[i].cls == cls) {
            *out = g_applied[i];
            return 0;
        }
    }
    return -1;
}
