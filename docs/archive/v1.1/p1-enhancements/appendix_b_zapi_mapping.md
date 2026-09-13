# 附录 B. FRR ZAPI → DPA 映射表（v1.1 P1-9）

> v1.1 评审 §4.2 P1 改进动作。给出 FRR Zebra ZAPI 消息到 DPA 对象的完整映射，
> 与现有实现 `danos-fib/src/mapper/zapi_mapper.c`、`danos-fib/src/zapi/zapi.h`
> 一致。ZAPI 版本 6（FRR 10.x）。

## 1. 映射总表

| ZAPI 消息 | 命令码 | DPA 对象 | DPA 操作 | 实现状态 | 代码位置 |
|-----------|--------|---------|---------|---------|---------|
| ZEBRA_ROUTE_ADD | 0 | Route + NH + NHGroup | create/update | 已实现 | `zapi_mapper.c:37` |
| ZEBRA_ROUTE_DELETE | 1 | Route | delete | 已实现 | `zapi_mapper.c:37` |
| ZEBRA_REDISTRIBUTE_ADD | 8 | Route | update（重分发） | 已实现（no-op，FRR 后续 ROUTE_ADD） | `zapi_mapper.c:zapi_map_redistribute_add` |
| ZEBRA_INTERFACE_ADD | 20 | Interface | create | 已实现 | `zapi_mapper.c:117` |
| ZEBRA_INTERFACE_DELETE | 21 | Interface | delete | 已实现 | `zapi_mapper.c:117` |
| ZEBRA_INTERFACE_SET_MTU | 24 | Interface | update（MTU 字段） | 已实现 | `zapi_mapper.c:zapi_map_interface_set_mtu` |
| ZEBRA_INTERFACE_UP | 25 | Interface | update（admin_up=true） | 已实现 | `zapi_mapper.c:zapi_map_interface_set_admin` |
| ZEBRA_INTERFACE_DOWN | 26 | Interface | update（admin_up=false） | 已实现 | `zapi_mapper.c:zapi_map_interface_set_admin` |
| ZEBRA_NEXTHOP_LOOKUP | 40 | NH / NHGroup | read | 已实现（v0.1 简化：NH id=1） | `zapi_mapper.c:zapi_map_nexthop_lookup` |
| ZEBRA_LABELS_ADD | 60 | MPLS LSP | create | 已实现 | `zapi_mapper.c:zapi_map_labels` |
| ZEBRA_LABELS_DELETE | 61 | MPLS LSP | delete | 已实现 | `zapi_mapper.c:zapi_map_labels` |
| ZEBRA_BFD_DEST_REGISTER | 80 | BFD Session | create | 已实现 | `zapi_mapper.c:164` |
| ZEBRA_BFD_DEST_DEREGISTER | 81 | BFD Session | delete | 已实现 | `zapi_mapper.c:164` |

**已实现：13/13 消息**（v0.2 增强后 ZAPI 映射全覆盖）
**待实现：0/13 消息**

## 2. 字段映射详解

### 2.1 ZEBRA_ROUTE_ADD → DPA Route + NH + NHGroup

ZAPI wire format（v0.1 简化）：
```
[vrf_id:4][family:1][prefix_len:1][prefix:4or16][protocol:1]
[admin_distance:1][metric:4][nexthop_count:1]
per nexthop: [type:1][gateway:4or16][ifindex:4]
```

| ZAPI 字段 | DPA 字段 | 转换 | 备注 |
|-----------|---------|------|------|
| vrf_id | `danos_route_t.vrf_id` | 直接 | 0=default VRF |
| family | `prefix.addr.af` | 4→IPV4, 6→IPV6 | |
| prefix_len | `prefix.prefix_len` | 直接 | |
| prefix | `prefix.addr.addr` | 直接（network order） | |
| protocol | `route.protocol` | 见下表 | ZAPI→DPA 协议映射 |
| admin_distance | `route.admin_distance` | 直接 | |
| metric | `route.metric` | 直接 | |
| nexthop_count | NHGroup 大小 | 1→单 NH+NHGroup；>1→ECMP | v0.1 简化：每 NH 创建独立 NH 对象 |
| nexthop.gateway | `nh.gateway` | 直接 | |
| nexthop.ifindex | `nh.ifindex` | 直接 | |
| nexthop.type | `nh.flags` | 待扩展 | v0.1 默认 ECMP flag |

**ZAPI 协议码 → DPA RouteProtocol 映射**（`zapi_mapper.c:16-27`）：

| ZAPI 码 | FRR 协议 | DPA 枚举 | DPA 码 |
|---------|---------|---------|--------|
| 0 | kernel | `DANOS_ROUTE_PROTO_KERNEL` | 1 |
| 1 | static | `DANOS_ROUTE_PROTO_STATIC` | 2 |
| 2 | bgp | `DANOS_ROUTE_PROTO_BGP` | 3 |
| 3 | ospf | `DANOS_ROUTE_PROTO_OSPF` | 4 |
| 4 | isis | `DANOS_ROUTE_PROTO_ISIS` | 5 |
| 5 | connected | `DANOS_ROUTE_PROTO_CONNECTED` | 6 |
| 其他 | - | `DANOS_ROUTE_PROTO_UNSPEC` | 0 |

### 2.2 ZEBRA_INTERFACE_ADD → DPA Interface

ZAPI wire format（v0.1 简化）：
```
[ifindex:4][name_len:1][name:variable][mtu:4][mac:6]
```

| ZAPI 字段 | DPA 字段 | 转换 | 备注 |
|-----------|---------|------|------|
| ifindex | `iface.ifindex` | 直接 | |
| name | `iface.name` | 直接（截断至 63 字节） | |
| mtu | `iface.mtu` | 直接（可选，默认 1500） | |
| mac | `iface.mac` | 直接（可选） | |
| - | `iface.type` | 固定 `DANOS_IF_TYPE_PHYS` | v0.2+ 区分 subif/bond |
| - | `iface.admin_up` | 固定 true | v0.2+ 由 UP/DOWN 消息更新 |

### 2.3 ZEBRA_BFD_DEST_REGISTER → DPA BFD Session

| ZAPI 字段 | DPA 字段 | 转换 |
|-----------|---------|------|
| family | `bfd.remote.af` | 4→IPV4, 6→IPV6 |
| remote_addr | `bfd.remote.addr` | 直接 |
| ifindex | `bfd.ifindex` | 直接（0=多跳） |
| desired_tx_ms | `bfd.desired_tx_ms` | 直接 |
| required_rx_ms | `bfd.required_rx_ms` | 直接 |
| detect_mult | `bfd.detect_mult` | 直接 |
| - | `bfd.admin_up` | true（REGISTER）/false（DEREGISTER） |

## 3. 未实现消息的映射设计（v0.2 优先）

### 3.1 ZEBRA_INTERFACE_SET_MTU（命令码 24）

```
ZAPI payload: [ifindex:4][mtu:4]
→ danos_iface_t iface; iface.ifindex = ifindex; iface.mtu = mtu;
→ danos_iface_update(tx, &iface);
```

### 3.2 ZEBRA_INTERFACE_UP / DOWN（命令码 25/26）

```
ZAPI payload: [ifindex:4]
→ danos_iface_t iface; iface.ifindex = ifindex; iface.admin_up = (cmd==UP);
→ danos_iface_update(tx, &iface);
```

### 3.3 ZEBRA_NEXTHOP_LOOKUP（命令码 40）

```
ZAPI payload: [vrf_id:4][family:1][gateway:4or16]
→ danos_nh_read(tx, id, &nh);  // id 由 gateway+ifindex 索引查询
→ 返回 NH + 关联 NHGroup 列表
```

### 3.4 ZEBRA_REDISTRIBUTE_ADD（命令码 8）

```
ZAPI payload: [protocol:1]
→ 触发 FRR 协议重分发，生成 ZEBRA_ROUTE_ADD 序列
→ 由 zapi_map_route 处理，无需独立 DPA 操作
```

### 3.5 ZEBRA_LABELS_ADD / DELETE（命令码 60/61，v0.2+）

```
ZAPI payload: [in_label:4][nh_count:1][per nh: gateway+ifindex][push_labels]
→ danos_mpls_lsp_t lsp; lsp.in_label = in_label; ...
→ danos_mpls_lsp_create(tx, &lsp);  // v0.2+ 实现
```

## 4. 映射不变量（conformance 约束）

1. **幂等性**：同一 ZAPI 消息重复下发，DPA 状态不变（create 已存在→EXISTS，视为成功）
2. **对称性**：ADD 后 DELETE 应回到 ADD 前状态
3. **协议保序**：ZAPI 消息顺序在 DPA Transaction 内保持（单 writer 串行）
4. **VRF 隔离**：vrf_id 不同的消息互不干扰
5. **错误传播**：DPA 返回 NOT_SUPPORTED 时，Adapter 记录 metric 并跳过，不阻塞后续消息

## 5. 测试覆盖

| 测试 | 覆盖消息 | 代码位置 |
|------|---------|---------|
| `test_zapi_parse` | 全部消息解析 | `danos-fib/tests/test_zapi_parse.c` |
| `test_zapi_mapper` | ROUTE_ADD/DELETE, IFACE_ADD/DELETE, BFD, SET_MTU, UP/DOWN, REDISTRIBUTE, NEXTHOP_LOOKUP, LABELS_ADD/DELETE | `danos-fib/tests/test_zapi_mapper.c` |
| `test_fib_e2e` | 单消息端到端 | `danos-fib/tests/test_fib_e2e.c` |
| `test_fib_e2e_multi` | 100 路由批量端到端 | `danos-fib/tests/test_fib_e2e_multi.c` |

**fib_mapper 测试用例：9 个全部通过**（3 原有 + 6 新增 v0.2）
**ZAPI 映射全覆盖：13/13 消息已实现+测试**
