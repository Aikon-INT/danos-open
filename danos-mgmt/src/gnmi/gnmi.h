/*
 * DANOS-Open Management: gNMI-like REST/JSON Server (E2)
 *
 * Implements a simplified gNMI-compatible interface over HTTP/JSON
 * instead of gRPC (which requires external libraries).
 *
 * Provides Get/Set semantics for DPA objects:
 *   GET  /gnmi/interfaces           → list all interfaces
 *   GET  /gnmi/interfaces/<id>      → get one interface
 *   GET  /gnmi/routes               → list all routes
 *   GET  /gnmi/vrfs                 → list all VRFs
 *   GET  /gnmi/capabilities         → DPA version + backend capabilities
 *   SET  /gnmi/interfaces           → create/update interface (JSON body)
 *   SET  /gnmi/routes               → create/update route (JSON body)
 *
 * v0.2 additions:
 *   GET  /gnmi/acl/tables           → list ACL tables
 *   SET  /gnmi/acl/tables           → create/update ACL table (JSON body)
 *   DELETE /gnmi/acl/tables/<id>    → delete ACL table
 *   GET  /gnmi/qos/policies         → list QoS policies
 *   SET  /gnmi/qos/policies         → create/update QoS policy (JSON body)
 *   DELETE /gnmi/qos/policies/<id>  → delete QoS policy
 *
 * gNMI Subscribe (v0.2) integrated into HTTP dispatch:
 *   POST /gnmi/subscribe[?obj_type=X&mask=Y]  → create subscription, returns sub_id
 *   SUBSCRIBE /gnmi/subscribe[?...]           → same (gNMI SUBSCRIBE method)
 *   GET  /gnmi/subscribe/<id>                 → poll notifications (JSON array)
 *   DELETE /gnmi/subscribe/<id>               → unsubscribe
 *
 * In production, a real gNMI server (gRPC) would be used. This REST
 * interface provides the same semantics for environments without gRPC.
 */

#ifndef DANOS_GNMI_H__
#define DANOS_GNMI_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>      /* size_t */
#include <danos/dpa.h>   /* danos_obj_type_t, danos_event_type_t */

#ifdef __cplusplus
extern "C" {
#endif

/* gNMI server context */
typedef struct {
    int listen_fd;
    uint16_t port;
    bool running;
    uint64_t requests_served;
} danos_gnmi_ctx_t;

/* Initialize gNMI server context */
void danos_gnmi_init(danos_gnmi_ctx_t *ctx, uint16_t port);

/* Start server (binds and listens). Returns 0 on success. */
int danos_gnmi_start(danos_gnmi_ctx_t *ctx);

/* Stop server */
void danos_gnmi_stop(danos_gnmi_ctx_t *ctx);

/* Process one request (for testing without network).
 * Returns JSON response string (caller must free).
 * Returns NULL on error. */
char *danos_gnmi_handle_request(const char *method, const char *path,
                                const char *body);

/* =========================================================================
 * gNMI Subscribe (v0.2)
 *
 * Implements gNMI Subscribe RPC semantics over the DPA event bus.
 * A subscription registers a callback for events matching (obj_type, mask).
 * The server pushes JSON-encoded notifications to the subscriber.
 *
 * In production, this would be a streaming gRPC RPC. Here we provide a
 * synchronous "poll" model: danos_gnmi_subscribe_poll() drains queued
 * events for a subscription into a JSON array.
 * ========================================================================= */

typedef struct danos_gnmi_sub {
    uint64_t                 id;          /* subscription id (from event bus) */
    uint32_t                 mask;        /* event mask */
    danos_obj_type_t         obj_type;    /* filter, or DANOS_OBJ_INVALID for all */
    struct danos_gnmi_sub   *next;
} danos_gnmi_sub_t;

/* Per-subscription notification queue (ring buffer, v0.2 simplified) */
#define DANOS_GNMI_QUEUE_SIZE 64

typedef struct {
    char    *entries[DANOS_GNMI_QUEUE_SIZE];  /* JSON-encoded notifications */
    size_t   head;
    size_t   tail;
    size_t   count;
    uint64_t dropped;       /* notifications dropped due to full queue */
} danos_gnmi_queue_t;

/* Subscribe to events. Returns subscription id (>0) or 0 on error.
 * `obj_type` = DANOS_OBJ_INVALID means all object types.
 * `mask` is a bitmask: bit N set => subscribe to event type N.
 *   e.g. (1u << DANOS_EVENT_OBJ_CREATED) | (1u << DANOS_EVENT_OBJ_UPDATED)
 *   or 0xFFFF for all events. */
uint64_t danos_gnmi_subscribe(danos_obj_type_t obj_type, uint32_t mask);

/* Unsubscribe by id. */
void danos_gnmi_unsubscribe(uint64_t sub_id);

/* Poll queued notifications for a subscription as a JSON array.
 * Caller must free the returned string. Returns "[]" if empty. */
char *danos_gnmi_subscribe_poll(uint64_t sub_id);

/* Get number of active subscriptions. */
size_t danos_gnmi_subscribe_count(void);

/* Initialize/shutdown the subscribe subsystem. */
void danos_gnmi_subscribe_init(void);
void danos_gnmi_subscribe_fini(void);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_GNMI_H__ */
