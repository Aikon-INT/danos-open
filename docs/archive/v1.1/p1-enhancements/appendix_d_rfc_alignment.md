# 附录 D. RFC 标准对齐表（v1.1 P1-14）

> v1.1 评审 §5.3 P1 改进动作。明确每个对象/功能对齐的 RFC/标准，
> 以及实现状态。

## 1. 控制面协议

| 功能 | RFC/标准 | DANOS 模块 | 实现状态 | 备注 |
|------|---------|-----------|---------|------|
| BGP-4 | RFC 4271 | FRR bgpd | FRR 提供 | 经 ZAPI 集成 |
| BGP GR | RFC 4724 | FRR bgpd + danos-ha | FRR + v0.2+ NSR | Graceful Restart |
| BGP LLGR | RFC 8542 | FRR bgpd | FRR 提供 | Long-lived GR |
| BGP TTL Security (GTSM) | RFC 5082 | danos-security | v0.2+ | BGP TTL 安全 |
| BGP TCP-AO | RFC 5925 | danos-security | v0.2+ | TCP 认证选项 |
| OSPFv2 | RFC 2328 | FRR ospfd | FRR 提供 | |
| OSPFv3 | RFC 5340 | FRR ospf6d | FRR 提供 | |
| IS-IS | RFC 5308 | FRR isisd | FRR 提供 | |
| BFD 单跳 | RFC 5881 | FRR bfdd + DPA BFD | 已实现 | `danos_bfd_t` |
| BFD 多跳 | RFC 5883 | FRR bfdd + DPA BFD | 已实现 | ifindex=0 |
| BFD 异步 | RFC 5880 | FRR bfdd | 已实现 | |
| BFD Echo | RFC 5880 §4.2 | FRR bfdd | v0.2+ | |

## 2. EVPN / VXLAN

| 功能 | RFC | DANOS 模块 | 实现状态 | 备注 |
|------|-----|-----------|---------|------|
| EVPN | RFC 7432 | danos-core (EVPN obj) | v0.2+ | EVPN NLRIs |
| EVPN-MH | RFC 7432 §8 | danos-ha | v0.2+ | Multi-homing, ESI |
| EVPN IRB | RFC 9161 | danos-core | v0.2+ | Integrated Routing/Bridging |
| VXLAN | RFC 7348 | danos-vpp | v0.2+ | |
| VXLAN-GPE | RFC 8365 | danos-vpp | v0.2+ | Generic Protocol Extension |
| EVPN-PIM | RFC 9161 | - | 未规划 | PIM-SM 控制平面 |

## 3. MPLS / Segment Routing

| 功能 | RFC | DANOS 模块 | 实现状态 |
|------|-----|-----------|---------|
| MPLS 架构 | RFC 3031 | danos-core (MPLS_LSP) | v0.2+ |
| LDP | RFC 5036 | FRR ldpd | FRR 提供 |
| RSVP-TE | RFC 3209 | FRR | v0.2+ |
| SR-MPLS | RFC 8660 | danos-core | v0.2+ |
| SR-TE | RFC 8667 | danos-core | v0.2+ |
| SRv6 | RFC 8754 | - | v0.5+ 规划 |

## 4. 多播

| 功能 | RFC | DANOS 模块 | 实现状态 |
|------|-----|-----------|---------|
| PIM-SM | RFC 7761 | FRR pimd + DPA mroute | v0.2+ |
| IGMPv3 | RFC 3376 | FRR | v0.2+ |
| MLDv2 | RFC 3810 | FRR | v0.2+ |
| BGP-MVPN | RFC 6514 | - | 未规划 |

## 5. 高可用

| 功能 | RFC | DANOS 模块 | 实现状态 |
|------|-----|-----------|---------|
| VRRP | RFC 5798 | danos-ha | v0.2+ |
| NSR | FRR nsr | danos-ha | v0.2+ |
| Graceful Restart | RFC 4724 | danos-ha | v0.2+ |

## 6. 管理面

| 功能 | RFC/标准 | DANOS 模块 | 实现状态 |
|------|---------|-----------|---------|
| NETCONF | RFC 6241 | danos-mgmt/netconf | 已实现（骨架） |
| NETCONF over SSH | RFC 6242 | danos-mgmt/netconf | v0.2+ |
| RESTCONF | RFC 8040 | danos-mgmt | v0.2+ |
| YANG 1.1 | RFC 7950 | danos-models | 已实现 |
| gNMI | gNMI spec | danos-mgmt/gnmi | 已实现 |
| gNOI | gNOI spec | danos-mgmt | v0.3+ |

## 7. 安全

| 功能 | RFC/标准 | DANOS 模块 | 实现状态 |
|------|---------|-----------|---------|
| SSH | RFC 4251-4254 | danos-security | v0.2+ |
| TLS 1.3 | RFC 8446 | danos-security | v0.2+ |
| mTLS | - | danos-security | v0.2+ |
| TACACS+ | RFC 8907 | danos-security | v0.2+ |
| RADIUS | RFC 2865 | danos-security | v0.2+ |
| IPsec | RFC 4301 | danos-security | v0.3+ |

## 8. 可观测性

| 功能 | 标准 | DANOS 模块 | 实现状态 |
|------|------|-----------|---------|
| OpenConfig | openconfig-public | danos-models | v0.2+ |
| gNMI Subscribe | gNMI spec | danos-observability | v0.2+ |
| IPFIX | RFC 7011 | danos-observability | v0.3+ |
| OpenTelemetry | OTel spec | danos-observability | v0.3+ |
| Prometheus | prometheus.io | danos-observability | v0.2+ |

## 9. 数据面

| 功能 | 标准 | DANOS 模块 | 实现状态 |
|------|------|-----------|---------|
| DPDK | dpdk.org | danos-vpp/danos-platform | v0.1（依赖） |
| VPP binary API | fd.io | danos-vpp | 已实现（mock） |
| P4Runtime | p4.org | danos-p4 | v0.4+ |
| P4_16 语言 | p4.org | danos-p4 | v0.4+ |
