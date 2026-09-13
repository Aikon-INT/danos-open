/* VPP adapter for the programming pipeline (v0.11) — see vpp_adapter.c */
#ifndef DANOS_VPP_ADAPTER_H__
#define DANOS_VPP_ADAPTER_H__
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
int danos_vpp_adapter_install(bool real, const char *sock_path);
#ifdef __cplusplus
}
#endif
#endif
