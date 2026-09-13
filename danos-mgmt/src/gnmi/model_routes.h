/*
 * DANOS-Open Management: Composite Route Management (v0.12)
 *
 * A northbound route is ONE semantic object, but it spans three DPA
 * objects (Nexthop -> NHGroup -> Route). This module owns that
 * composition for every northbound entry point:
 *
 *   set   : parse "prefix + gateway + oif + vrf" -> create/update the
 *           three objects in a single transaction (idempotent)
 *   delete: cascade-remove route -> group -> nexthops
 *   get   : render a route with its resolved gateway/oif
 *
 * Used by gNMI Set/Get, the CLI and NETCONF alike (single source of
 * truth for the composite semantics).
 */

#ifndef DANOS_MODEL_ROUTES_H__
#define DANOS_MODEL_ROUTES_H__

#include <danos/dpa.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse "a.b.c.d/len" (IPv4 only in v0.12). Returns DANOS_OK. */
danos_status_t gnmi_parse_prefix(const char *s, danos_ip_prefix_t *out);

/* Parse "a.b.c.d" into an IPv4 danos_ip_addr_t. */
danos_status_t gnmi_parse_ipv4(const char *s, danos_ip_addr_t *out);

/* Composite set: creates or updates NH + NHGroup + Route in one
 * transaction. Returns DANOS_OK; the route is then visible in desired
 * state (the programming pipeline pushes it to the backend). */
danos_status_t gnmi_route_set(danos_vrf_id_t vrf_id,
                              const danos_ip_prefix_t *prefix,
                              const danos_ip_addr_t *gateway,
                              uint32_t oif);

/* Composite delete: removes the route (matched by vrf+prefix+static
 * proto), its group, and the group's member NHs. Returns OK even when
 * nothing matched (idempotent delete); NOT_FOUND propagates from
 * backend failures only. */
danos_status_t gnmi_route_delete(danos_vrf_id_t vrf_id,
                                 const danos_ip_prefix_t *prefix);

/* Render route + resolved gateway/oif as compact JSON into out. */
danos_status_t gnmi_route_to_json(const danos_route_t *route,
                                  char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_MODEL_ROUTES_H__ */
