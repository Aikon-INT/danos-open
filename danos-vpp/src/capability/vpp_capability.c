/*
 * VPP Backend Capability registration (D7)
 *
 * Registers VPP backend with the DPA capability registry,
 * advertising supported object types and limits.
 */

#include <danos/dpa.h>
#include <string.h>

static const char *kRouteFeatures[] = {"ipv4", "ipv6", "multipath"};
static const char *kEvpnFeatures[]  = {"type2", "type3", "type5", "irb", "mh"};
static const char *kAclFeatures[]   = {"ingress", "egress", "ipv4", "ipv6", "l4"};
static const char *kQosFeatures[]   = {"policer-single-rate", "policer-dual-rate"};

static const danos_capability_t kVppCaps[] = {
    {DANOS_OBJ_IFACE,     true, 1024,    0, NULL,            NULL},
    {DANOS_OBJ_VLAN,      true, 4094,    0, NULL,            NULL},
    {DANOS_OBJ_VRF,       true, 4096,    0, NULL,            NULL},
    {DANOS_OBJ_ROUTE,     true, 1000000, 3, kRouteFeatures,  NULL},
    {DANOS_OBJ_NEXTHOP,   true, 1000000, 0, NULL,            NULL},
    {DANOS_OBJ_NHGROUP,   true, 500000,  0, NULL,            NULL},
    {DANOS_OBJ_ACL,       true, 16384,   5, kAclFeatures,    NULL},
    {DANOS_OBJ_QOS,       true, 1024,    2, kQosFeatures,    NULL},
    {DANOS_OBJ_MPLS_LSP,  true, 100000,  0, NULL,            NULL},
    {DANOS_OBJ_TUNNEL,    true, 4096,    0, NULL,            NULL},
    {DANOS_OBJ_EVPN,      true, 4096,    5, kEvpnFeatures,   NULL},
    {DANOS_OBJ_MULTICAST, false, 0,      0, NULL,            NULL},
    {DANOS_OBJ_BFD,       true, 1024,    0, NULL,            NULL},
};

int danos_vpp_capability_register(void)
{
    danos_backend_info_t be;
    memset(&be, 0, sizeof(be));
    strncpy(be.name, "vpp", sizeof(be.name) - 1);
    be.api_version = danos_dpa_get_version();
    be.cap_count = sizeof(kVppCaps) / sizeof(kVppCaps[0]);
    be.caps = kVppCaps;
    return (int)danos_backend_register(&be);
}
