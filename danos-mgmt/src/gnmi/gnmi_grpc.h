/*
 * DANOS-Open Management: gNMI gRPC Server (v0.3)
 *
 * A real gNMI server: protobuf-encoded gNMI messages over gRPC framing
 * on HTTP/2 (h2c — cleartext, no TLS; a TLS wrapper can be added in
 * front without changing this code).
 *
 * Service: gnmi.gNMI (per openconfig/gnmi), methods:
 *   /gnmi.gNMI/Capabilities
 *   /gnmi.gNMI/Get
 *   /gnmi.gNMI/Set
 *   /gnmi.gNMI/Subscribe   (ONCE mode: initial update + sync_response)
 *
 * Get reads from the DPA desired-state store; Set writes via DPA
 * transactions (same code path as the CLI/NETCONF front ends).
 */

#ifndef DANOS_GNMI_GRPC_H__
#define DANOS_GNMI_GRPC_H__

#include <danos/core/state_store.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      listen_fd;
    uint16_t port;
    _Atomic bool running;
    _Atomic uint64_t rpcs_served;
    danos_state_store_t *store;   /* borrowed; NULL = use g_default_store */
} danos_gnmi_grpc_ctx_t;

void danos_gnmi_grpc_init(danos_gnmi_grpc_ctx_t *ctx, uint16_t port);

/* Bind the server to a state store (desired state is read by Get) */
void danos_gnmi_grpc_set_store(danos_gnmi_grpc_ctx_t *ctx,
                               danos_state_store_t *store);

/* Start listening (background accept thread). 0 on success. */
int  danos_gnmi_grpc_start(danos_gnmi_grpc_ctx_t *ctx);
void danos_gnmi_grpc_stop(danos_gnmi_grpc_ctx_t *ctx);

/* Process one RPC on an already-accepted socket (for testing):
 * performs the full HTTP/2 handshake and serves requests until the
 * peer closes. Returns 0 on success. */
int danos_gnmi_grpc_serve_fd(int fd);

/* Subscribe streaming: sends response headers, then ONCE/POLL/STREAM
 * notifications on the open h2 stream. Returns 0 when the stream is
 * finished (client closed / mode complete), negative on error. */
#include <stdint.h>
#include <stddef.h>
int gnmi_handle_subscribe(void *c, uint32_t stream,
                          const uint8_t *req, size_t req_len);

/* Request handlers, exposed for tests (protobuf in/out) */
int gnmi_handle_capabilities(const uint8_t *req, size_t req_len,
                             uint8_t *resp, size_t resp_cap);
int gnmi_handle_get(danos_state_store_t *store,
                    const uint8_t *req, size_t req_len,
                    uint8_t *resp, size_t resp_cap);
int gnmi_handle_set(danos_state_store_t *store,
                    const uint8_t *req, size_t req_len,
                    uint8_t *resp, size_t resp_cap);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_GNMI_GRPC_H__ */
