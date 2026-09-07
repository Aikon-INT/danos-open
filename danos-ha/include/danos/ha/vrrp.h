/*
 * DANOS-Open HA: VRRP Interface
 *
 * VRRPv3 (RFC 5798) for default gateway redundancy.
 * Design: v1.1/ha/ha_design.md §21.4
 */

#ifndef DANOS_HA_VRRP_H__
#define DANOS_HA_VRRP_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t danos_vrrp_id_t; /* VRID, 1-255 */

typedef struct {
    danos_vrrp_id_t vrid;       /* virtual router ID (key) */
    uint32_t        ifindex;    /* interface */
    uint8_t         priority;   /* 1-254, higher = more preferred */
    bool            preempt;    /* preempt lower-priority master */
    uint32_t        advert_int_ms; /* advertisement interval (ms) */
    char            virtual_ip[46]; /* virtual IP */
    bool            ipv6;       /* IPv6 mode */
    /* BFD联动: BFD session down → reduce priority */
    bool            bfd_link;   /* enable BFD linkage */
    uint64_t        bfd_session_id; /* linked BFD session */
    uint8_t         bfd_down_priority; /* priority when BFD down */
} danos_vrrp_session_t;

typedef enum {
    DANOS_VRRP_STATE_INIT    = 0,
    DANOS_VRRP_STATE_BACKUP  = 1,
    DANOS_VRRP_STATE_MASTER  = 2,
} danos_vrrp_state_t;

int danos_vrrp_create(const danos_vrrp_session_t *sess);
int danos_vrrp_update(const danos_vrrp_session_t *sess);
int danos_vrrp_delete(danos_vrrp_id_t vrid, uint32_t ifindex);
int danos_vrrp_read(danos_vrrp_id_t vrid, uint32_t ifindex,
                    danos_vrrp_session_t *out);

danos_vrrp_state_t danos_vrrp_get_state(danos_vrrp_id_t vrid, uint32_t ifindex);

int danos_vrrp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_HA_VRRP_H__ */
