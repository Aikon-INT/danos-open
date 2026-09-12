/*
 * Mock VPP server for protocol conformance tests (v0.3).
 *
 * Implements the server side of the VPP binary API socket transport
 * (handshake + echo replies) and the stat segment socket protocol
 * (SCM_RIGHTS fd passing), so the real client code path can be
 * exercised end-to-end without VPP.
 */
#ifndef DANOS_MOCK_VPP_SERVER_H__
#define DANOS_MOCK_VPP_SERVER_H__

#include <stdint.h>

/* Binary API mock server */
int  mock_vpp_start(const char *path);        /* 0 on success */
void mock_vpp_stop(void);
void mock_vpp_get_last_request(uint16_t *msg_id, uint8_t *body,
                               uint32_t *body_len, uint32_t max_body);
/* Fake message table ids handed to the client */
#define MOCK_MSGID_CONTROL_PING        0x0100
#define MOCK_MSGID_SW_IF_SET_FLAGS     0x0101
#define MOCK_MSGID_IP_ROUTE_ADD_DEL    0x0102
#define MOCK_MSGID_IP_TABLE_ADD_DEL    0x0103
#define MOCK_MSGID_IP_NEIGHBOR_ADD_DEL 0x0104

/* Stat segment mock server: serves one segment containing
 * SCALAR "/sys/node/ip4-input" = 1000000 and SIMPLE_COUNTER
 * "/if/0/rx-packets" = 5000 (sum over 2 workers). */
int  mock_statseg_start(const char *path);
void mock_statseg_stop(void);

#endif
