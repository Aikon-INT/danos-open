# VPP Backend 实现路径（v1.1 P1-10）

> v1.1 评审 §4.3 P1 改进动作。明确 VPP Backend 的控制通道、对象映射、
> 性能路径、故障注入设计。与现有实现 `danos-vpp/src/` 一致。

## 1. 控制通道

### 1.1 VPP Binary API（控制面）

| 维度 | 选择 | 理由 |
|------|------|------|
| 传输 | VPP binary API over shared memory | 高性能、VPP 原生 |
| 客户端 | vat2 / stat segment client | C 语言，与 DPA Core 同进程 |
| 编解码 | VPP message ID 动态注册 | VPP 版本无关 |
| **不用** | memif | memif 是数据面接口，非控制面 |

**连接管理**（`vpp_api.h`）：
- `danos_vpp_api_connect()` — 建立 binary API 通道
- `danos_vpp_api_connect_stat()` — 建立 stat segment 通道
- `danos_vpp_api_reconnect()` — 断线重连（指数退避）
- `danos_vpp_api_is_connected()` — 健康检查

### 1.2 Stat Segment（可观测性）

- `danos_vpp_api_stat_query(name)` — 命名计数器查询（如 `/if/rx-packets`）
- 用于 Reconciler 的 VERIFY 阶段和 Telemetry 上报

## 2. 对象映射（DPA → VPP）

### 2.1 映射总表

| DPA 对象 | VPP API | 映射函数 | 状态 |
|---------|---------|---------|------|
| Interface | `sw_interface_create/delete` + `sw_interface_set_flags` | `vpp_map_iface_create/delete` | 已实现（mock） |
| VRF | `ip_table_add/del`（FIB table id = VRF id） | `vpp_map_vrf_create/delete` | 已实现（mock） |
| Route | `ip_route_add/del`（FIB index = VRF id） | `vpp_map_route_create/delete` | 已实现（mock） |
| NextHop | `ip_neighbor_add/del` + path | `vpp_map_nh_create` | 已实现（mock） |
| NHGroup | multipath path list | `vpp_map_nhgroup_create` | 已实现（mock） |
| ACL | `classify_add_table` + `classify_add_del_session` | `vpp_map_acl_table_create/rule_add` | 已实现（mock） |
| QoS | `policer_add/del` + `qos_record` | `vpp_map_qos_policy_create/delete` | 已实现（mock） |

### 2.2 字段映射详解

#### Interface → VPP sw_interface

| DPA 字段 | VPP 概念 | VPP API 字段 |
|---------|---------|-------------|
| `ifindex` | `sw_if_index` | 直接映射 |
| `name` | interface name | `sw_interface_dump` 匹配 |
| `type` | interface type | PHYS→DPDK port, SUBIF→subif, BOND→bond, VXLAN→vxlan tunnel |
| `mtu` | MTU | `sw_interface_set_mtu` |
| `mac` | MAC | `sw_interface_set_mac` |
| `admin_up` | admin state | `sw_interface_set_flags` (admin_up/down) |
| `parent_ifindex` + `outer_vlan` | sub-interface | `create_subif` |

#### VRF → VPP FIB table

| DPA 字段 | VPP 概念 | VPP API |
|---------|---------|---------|
| `vrf_id` | FIB table id | `ip_table_add/del` (table_id = vrf_id) |
| `ipv4_active` | IPv4 FIB | `ip_table_add` (family=AF_INET) |
| `ipv6_active` | IPv6 FIB | `ip_table_add` (family=AF_INET6) |

#### Route + NH + NHGroup → VPP ip_route

| DPA 字段 | VPP 概念 | VPP API |
|---------|---------|---------|
| `vrf_id` | FIB table index | `ip_route_add` (table_id) |
| `prefix` | route prefix | `ip_route_add` (prefix) |
| `protocol` | route protocol | VPP 不区分协议，由 FRR 维护协议语义 |
| `admin_distance` | - | VPP 不支持，由 DPA Reconciler 处理多协议优选 |
| `nhgroup_id` | path list | `ip_route_add` (paths[]) |
| NH `gateway` | path next hop | `paths[i].nh` |
| NH `ifindex` | path sw_if_index | `paths[i].sw_if_index` |
| NH `weight` | path weight | `paths[i].weight`（加权 ECMP） |
| NH `flags & MPLS_PUSH` | label stack | `paths[i].n_labels`, `paths[i].label_stack` |
| NH `flags & MPLS_POP` | pop operation | `paths[i].lookup_next_index` |

#### ACL → VPP classify

| DPA 字段 | VPP 概念 | VPP API |
|---------|---------|---------|
| `table_id` | classify table | `classify_add_table` (table_id) |
| `bind_ifindex` + `ingress` | apply mask | `classify_set_interface_ip_table` |
| `rule.priority` | session priority | `classify_add_del_session` (hit_index) |
| `match.fields_mask` | match mask | `classify_add_del_session` (mask) |
| `match.src_ip/dst_ip` | L3 match | mask fields |
| `match.l4_proto/src_port/dst_port` | L4 match | mask fields |
| `action.action` | action | PERMIT→advance, DENY→drop, MIRROR→mirror, REDIRECT→redirect, POLICE→policer |

#### QoS → VPP policer

| DPA 字段 | VPP 概念 | VPP API |
|---------|---------|---------|
| `policy_id` | policer name | `policer_add` (name) |
| `cir_bps` | CIR | `policer_add` (cir) |
| `cb_bytes` | committed burst | `policer_add` (cb) |
| `pir_bps` | PIR | `policer_add` (pir, 0=单速率) |
| `pb_bytes` | peak burst | `policer_add` (pb) |
| `conform/exceed/violate_dscp` | mark | `policer_add` (conform/exceed/violate action) |

## 3. 性能路径

### 3.1 数据面

| 维度 | 配置 |
|------|------|
| PMD | DPDK poll-mode driver（vfio-pci / uio_pci_generic） |
| 巨型帧 | MTU 9000（`iface.mtu` 上限 9216） |
| NUMA 亲和 | NIC + VPP worker 同 NUMA，由 `danos-platform` 分配 |
| RSS | 多队列，VPP worker 按队列绑定 |
| 批量 | VPP batch API（`ip_route_add` 批量），减少 shared memory 往返 |

### 3.2 控制面

| 维度 | 配置 |
|------|------|
| Binary API | shared memory，零拷贝 |
| 批量提交 | DPA Transaction COMMIT 阶段批量下发 VPP API |
| 异步 | VPP reply 用 future/promise，不阻塞 DPA 事务 |

## 4. 故障注入与容错

| 场景 | 处理 |
|------|------|
| VPP API 断连 | `danos_vpp_api_reconnect()` 指数退避（1s, 2s, 4s, ... 上限 60s） |
| 断连期间 DPA 操作 | 返回 `DANOS_ERR_BACKEND_DOWN`，Transaction 进入 ABORT |
| 重连后 | Reconciler 触发全量对账，重编程 Desired 状态 |
| VPP API 超时 | `DANOS_ERR_TX_TIMEOUT`，Transaction ABORT + 告警 |
| VPP 返回错误 | 映射到 DPA 错误码（如 VPP memif not enough → `NO_CAPACITY`） |
| Verify 失败 | `DANOS_ERR_VERIFY_FAIL`，Reconciler 重试 |

## 5. Mock 模式（v0.1）

v0.1 不依赖真实 VPP，`vpp_api_stub.c` 提供 mock 实现：
- `danos_vpp_api_enable_mock()` — 启用 mock
- API 调用计数（`vpp_api_stats_t`）— 验证映射正确性
- 命名 stat 计数器（`stat_set/stat_list`）— 验证 telemetry 路径
- 真实 VPP 连接需部署环境（D1 状态：mock 通过）

## 6. 与现有代码的对齐

| 元素 | 代码位置 | 一致性 |
|------|---------|--------|
| API 客户端接口 | `danos-vpp/src/api/vpp_api.h` | §1 一致 |
| Mapper 接口 | `danos-vpp/src/mapper/vpp_mapper.h:44-65` | §2 一致 |
| Mapper 实现 | `danos-vpp/src/mapper/vpp_mapper.c` | §2 字段映射一致 |
| Capability 上报 | `danos-vpp/src/capability/vpp_capability.c` | §2 对象范围一致 |
| Mock stub | `danos-vpp/src/api/vpp_api_stub.c` | §5 一致 |
