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
 *   SET  /gnmi/interfaces           → create/update interface (JSON body)
 *   SET  /gnmi/routes               → create/update route (JSON body)
 *
 * In production, a real gNMI server (gRPC) would be used. This REST
 * interface provides the same semantics for environments without gRPC.
 */

#ifndef DANOS_GNMI_H__
#define DANOS_GNMI_H__

#include <stdint.h>
#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif

#endif /* DANOS_GNMI_H__ */
