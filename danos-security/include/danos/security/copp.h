/*
 * DANOS-Open Security: CoPP Interface
 *
 * Control Plane Policing: classify and rate-limit control traffic.
 * Design: v1.1/security/security_architecture.md §20.6
 */

#ifndef DANOS_SECURITY_COPP_H__
#define DANOS_SECURITY_COPP_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CoPP traffic classes */
typedef enum {
    DANOS_COPP_CLASS_BGP     = 1,
    DANOS_COPP_CLASS_OSPF    = 2,
    DANOS_COPP_CLASS_ISIS    = 3,
    DANOS_COPP_CLASS_BFD     = 4,
    DANOS_COPP_CLASS_SSH     = 5,
    DANOS_COPP_CLASS_NETCONF = 6,
    DANOS_COPP_CLASS_GNMI    = 7,
    DANOS_COPP_CLASS_ARP     = 8,
    DANOS_COPP_CLASS_ICMP    = 9,
    DANOS_COPP_CLASS_DHCP    = 10,
    DANOS_COPP_CLASS_NTP     = 11,
    DANOS_COPP_CLASS_DNS     = 12,
    DANOS_COPP_CLASS_EXCEPTION = 13, /* catch-all */
} danos_copp_class_t;

/* CoPP policer parameters (single-rate, two-color) */
typedef struct {
    danos_copp_class_t cls;
    uint64_t cir_bps;       /* committed information rate (bits/sec) */
    uint64_t cb_bytes;      /* committed burst size (bytes) */
    bool     drop_on_exceed; /* true = drop, false = mark DSCP */
    uint8_t  exceed_dscp;   /* DSCP to mark on exceed (if !drop) */
} danos_copp_policy_t;

/* Default CoPP policies (§20.6.1).
 * Returns 0 on success. Caller does NOT free (static data). */
const danos_copp_policy_t *danos_copp_defaults(int *count);

/* Apply a CoPP policy to a traffic class.
 * In production, this programs VPP classify + policer.
 * Returns 0 on success. */
int danos_copp_apply(const danos_copp_policy_t *policy);

/* Get CoPP policy for a class.
 * Returns 0 on success, -1 if class not configured. */
int danos_copp_get(danos_copp_class_t cls, danos_copp_policy_t *out);

/* Initialize CoPP with default policies. */
int danos_copp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_SECURITY_COPP_H__ */
