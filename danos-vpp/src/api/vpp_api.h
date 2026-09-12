/*
 * DANOS-Open VPP Backend: Binary API Client Interface (D1)
 */

#ifndef DANOS_VPP_API_H__
#define DANOS_VPP_API_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize VPP API client */
int  danos_vpp_api_init(void);

/* Enable mock mode (no VPP required) */
void danos_vpp_api_enable_mock(void);

/* Connect to VPP binary API and stat segment */
int  danos_vpp_api_connect(void);
int  danos_vpp_api_connect_stat(void);
void danos_vpp_api_disconnect(void);
int  danos_vpp_api_reconnect(void);

/* Message send/receive. The payload must NOT include client_index/
 * context (prepended automatically) nor the msg_id/framing. */
int  danos_vpp_api_send(uint16_t msg_id, const void *payload, uint32_t payload_size);
int  danos_vpp_api_recv(uint8_t *buf, uint32_t buf_size);

/* Request/reply transaction: send and wait for the matching reply
 * (context correlation). Returns reply length (msg_id + struct),
 * negative on error/timeout. */
int  danos_vpp_api_transact(uint16_t msg_id, const uint8_t *payload,
                            uint32_t payload_size, uint8_t *reply,
                            uint32_t reply_size);

/* Name -> msg_id resolution (from the handshake message table) */
bool danos_vpp_api_lookup_msg_id(const char *name, uint16_t *msg_id);
uint32_t danos_vpp_api_client_index(void);
uint32_t danos_vpp_api_msg_table_count(void);
const char *danos_vpp_api_sock_path(void);
void danos_vpp_api_set_sock_path(const char *path);
void danos_vpp_api_set_stat_sock_path(const char *path);

/* Stat segment */
uint64_t danos_vpp_api_stat_query(const char *name);

/* Mock stat segment: register/set a named counter (for testing) */
void danos_vpp_api_stat_set(const char *name, uint64_t value);

/* Mock stat segment: list all counters (returns count, fills names/values if non-NULL) */
int  danos_vpp_api_stat_list(const char **names, uint64_t *values, int max_entries);

/* Mock stat segment: reset all counters */
void danos_vpp_api_stat_reset(void);

/* Status */
bool danos_vpp_api_is_connected(void);
bool danos_vpp_api_is_mock(void);
void danos_vpp_api_get_stats(uint64_t *msgs_sent, uint64_t *msgs_received,
                              uint64_t *connect_count, uint64_t *reconnect_count);
void danos_vpp_api_get_last_mock_msg(uint16_t *msg_id, uint32_t *msg_size);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_VPP_API_H__ */
