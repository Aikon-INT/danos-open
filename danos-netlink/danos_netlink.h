/*
 * DANOS-Open Backend: Linux kernel netlink adapter (v0.9, ADR-0007)
 *
 * Programs interface admin state and IPv4 routes into the kernel via
 * rtnetlink — the "kernel backend" of the DPA. Two modes:
 *
 *   mock  — in-memory FIB/interface tables, queryable for tests;
 *           no privileges required (default)
 *   real  — rtnetlink to the local kernel (requires CAP_NET_ADMIN);
 *           intended for container/namespace deployments
 *
 * The adapter implements danos_backend_ops_t and is idempotent.
 */

#ifndef DANOS_NETLINK_H__
#define DANOS_NETLINK_H__

#include <danos/core/backend_ops.h>
#include <danos/dpa.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the adapter. real=false selects the mock tables. */
int danos_netlink_init(bool real);

/* Install as the active danos_backend_ops (after init). */
void danos_netlink_register_backend(void);

void danos_netlink_shutdown(void);

/* ---- mock-mode query API (test assertions) ----------------------------- */

/* Number of IPv4 routes in the mock FIB. */
unsigned danos_netlink_mock_route_count(void);

/* Exact-match a mock route (network byte order addr, prefix len). */
bool danos_netlink_mock_route_exists(const uint8_t addr[4], uint8_t len,
                                     danos_vrf_id_t table_id);

/* Mock interface admin state. Returns false if unknown. */
bool danos_netlink_mock_iface_up(danos_ifindex_t ifindex);

/* Real-mode info. */
bool danos_netlink_is_real(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_NETLINK_H__ */
