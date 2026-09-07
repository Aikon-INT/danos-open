/*
 * DANOS-Open HA: BFD Multi-hop Interface
 *
 * BFD multihop for remote peer fault detection (~3s detection).
 * Design: v1.1/ha/ha_design.md §21.2
 */

#ifndef DANOS_HA_BFD_H__
#define DANOS_HA_BFD_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t danos_bfd_id_t;

typedef struct {
    danos_bfd_id_t id;          /* session ID (key) */
    char           remote[46];  /* remote IP */
    char           local[46];   /* local IP */
    bool           multihop;    /* true = multihop (TTL > 1) */
    uint32_t       desired_tx_ms;  /* desired transmit interval */
    uint32_t       required_rx_ms; /* required receive interval */
    uint8_t        detect_mult;    /* detect multiplier */
} danos_bfd_session_t;

typedef enum {
    DANOS_BFD_STATE_DOWN = 0,
    DANOS_BFD_STATE_INIT = 1,
    DANOS_BFD_STATE_UP   = 2,
    DANOS_BFD_STATE_ADMINDOWN = 3,
} danos_bfd_state_t;

/* Create/update/delete/read BFD session */
int danos_bfd_create(const danos_bfd_session_t *sess);
int danos_bfd_update(const danos_bfd_session_t *sess);
int danos_bfd_delete(danos_bfd_id_t id);
int danos_bfd_read(danos_bfd_id_t id, danos_bfd_session_t *out);

/* Get current state */
danos_bfd_state_t danos_bfd_get_state(danos_bfd_id_t id);

/* Register callback for state change */
typedef void (*danos_bfd_cb_t)(danos_bfd_id_t id, danos_bfd_state_t old_state,
                               danos_bfd_state_t new_state, void *user);
int danos_bfd_register_callback(danos_bfd_cb_t cb, void *user);

/* Initialize BFD module */
int danos_bfd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_HA_BFD_H__ */
