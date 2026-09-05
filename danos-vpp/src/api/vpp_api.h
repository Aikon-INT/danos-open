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

/* Message send/receive */
int  danos_vpp_api_send(uint16_t msg_id, const void *payload, uint32_t payload_size);
int  danos_vpp_api_recv(uint8_t *buf, uint32_t buf_size);

/* Stat segment */
uint64_t danos_vpp_api_stat_query(const char *name);

/* Status */
bool danos_vpp_api_is_connected(void);
void danos_vpp_api_get_stats(uint64_t *msgs_sent, uint64_t *msgs_received,
                              uint64_t *connect_count, uint64_t *reconnect_count);
void danos_vpp_api_get_last_mock_msg(uint16_t *msg_id, uint32_t *msg_size);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_VPP_API_H__ */
