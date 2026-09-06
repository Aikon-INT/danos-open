# Capability Schema 与 DPA Object 关系形式化（v1.1 §3.4）

> v1.1 评审 §3.4 P0 收尾文档。明确 Capability 与 DPA Object 的形式化关系，
> 给出 YANG + Protobuf 双发 schema 示例，与现有实现（`danos_dpa.h`、
> `dpa.proto`、`danos-vpp/src/capability/vpp_capability.c`）一致。

## 1. 形式化定义

### 1.1 Capability 是 DPA Object 的元数据

Capability **不是独立对象**，而是 DPA Object 类型的**元数据声明**，由 Backend
在启动时上报，DPA Core 在 Transaction 的 VALIDATE 阶段校验。

```
Capability : ObjectType × Backend → {
    supported   : bool,
    max_count   : uint32,        // 0 = unlimited
    features    : [string],      // 细粒度特性标志
    constraints : { field → range|enum|regex }  // per-field 约束
}
```

### 1.2 校验时机

| 阶段 | 动作 |
|------|------|
| Backend 启动 | 调用 `danos_backend_register()` 上报 `BackendInfo`（含 `Capability[]`） |
| `danos_tx_validate()` | 对每个 Desired 对象查 `danos_capability_query()`，校验 supported / max_count / features / constraints |
| 校验失败 | 返回 `DANOS_ERR_NOT_SUPPORTED` / `DANOS_ERR_NO_CAPACITY` / `DANOS_ERR_CAPABILITY` |
| Reconcile | 周期对账 Programmed 状态是否仍在 Capability 范围内（Backend 能力变更时） |

### 1.3 Capability 变更事件

Backend 能力变更（如 VPP 插件加载/卸载）经事件总线上报
`DANOS_EVENT_CAPABILITY`，Reconciler 触发受影响对象的重校验。

## 2. ObjectType 枚举（与 `danos_dpa.h` 一致）

| 值 | ObjectType | v0.1 状态 | 说明 |
|---|-----------|----------|------|
| 1 | IFACE | 已实现 | 物理口/子接口/Bond/Loopback/VXLAN/GRE/Bridge |
| 2 | VLAN | 已实现 | 802.1Q VLAN，1..4094 |
| 3 | VRF | 已实现 | 0..4095，0=default |
| 4 | ROUTE | 已实现 | IPv4/IPv6，多协议，ECMP |
| 5 | NEXTHOP | 已实现 | 含 MPLS label stack、tunnel 标志 |
| 6 | NHGROUP | 已实现 | ECMP/加权 ECMP/backup，最多 64 NH |
| 7 | ACL | 已实现 | L2-L4 match，6 种 action |
| 8 | QOS | 已实现 | v0.1 单/双速率 policer；v0.2+ HQoS |
| 9 | MPLS_LSP | ABI 声明 | v0.2+ 实现 |
| 10 | TUNNEL | ABI 声明 | v0.2+ 实现（VXLAN/GRE/IPIP/SR-TE） |
| 11 | EVPN | ABI 声明 | v0.2+ 实现（type2/3/5, IRB, MH） |
| 12 | MULTICAST | ABI 声明 | v0.2+ 实现 |
| 13 | BFD | 已实现 | 单跳/多跳 |

## 3. Protobuf Schema（与 `dpa.proto` 一致）

```protobuf
// 已在 danos-dpa/dpa.proto 定义，此处为引用快照
message Capability {
  ObjectType type = 1;
  bool supported = 2;
  uint32 max_count = 3;       // 0 = unlimited
  repeated string features = 4;
  string constraints_json = 5;  // per-field 约束，JSON 编码
}

message BackendInfo {
  string name = 1;            // "vpp", "ovs-dpdk", "p4-dpdk"
  Version api_version = 2;
  repeated Capability capabilities = 3;
}

service DpaService {
  rpc GetBackends(GetBackendsRequest) returns (stream BackendInfo);
  rpc QueryCapability(CapabilityRequest) returns (CapabilityResponse);
}
```

**`constraints_json` 编码约定**（v0.1 用 JSON，v0.2 可演进为结构化 Protobuf `Any`）：

```json
{
  "mtu": {"range": [68, 9216]},
  "vlan_id": {"range": [1, 4094]},
  "label_count": {"range": [0, 3]},
  "nhgroup_size": {"range": [1, 64]},
  "actions": {"enum": ["permit","deny","mirror","redirect","police","set_dscp"]}
}
```

## 4. YANG Schema（新增 `danos-models/yang/danos-capability/`）

> 与 Protobuf `Capability` 消息**语义等价、字段一一对应**，供 gNMI/NETCONF
> 管理面查询 Backend 能力。

```yang
module danos-capability {
  yang-version 1.1;
  namespace "urn:danos:capability";
  prefix dcap;

  import danos-types { prefix dt; }

  description
    "DANOS-Open capability model.
     Capability is metadata of DPA ObjectType, reported by backends.
     Copyright (c) 2026 DANOS-Open Project. Apache-2.0.";

  revision 2026-09-06 {
    description "Initial version for v0.1 (v1.1 §3.4).";
  }

  typedef object-type {
    type enumeration {
      enum iface     { value 1; }
      enum vlan      { value 2; }
      enum vrf       { value 3; }
      enum route     { value 4; }
      enum nexthop   { value 5; }
      enum nhgroup   { value 6; }
      enum acl       { value 7; }
      enum qos       { value 8; }
      enum mpls-lsp  { value 9; }
      enum tunnel    { value 10; }
      enum evpn      { value 11; }
      enum multicast { value 12; }
      enum bfd       { value 13; }
    }
  }

  container backends {
    config false;
    description "Registered DPA backends and their capability profiles.";

    list backend {
      key "name";
      leaf name { type string { length "1..63"; } }
      container api-version {
        leaf major { type uint16; }
        leaf minor { type uint16; }
        leaf patch { type uint16; }
      }
      list capability {
        key "object-type";
        leaf object-type { type object-type; }
        leaf supported { type boolean; }
        leaf max-count { type uint32; }  // 0 = unlimited
        leaf-list features { type string; }
        // per-field 约束，JSON 编码（与 Protobuf constraints_json 对齐）
        leaf constraints-json { type string; }
      }
    }
  }
}
```

**gNMI 查询路径示例**：
- `/backends/backend[name=vpp]/capability[object-type=route]/supported`
- `/backends/backend[name=vpp]/capability[object-type=evpn]/features`

## 5. Backend Capability Profile 示例

### 5.1 VPP Backend（与 `vpp_capability.c` 一致）

```json
{
  "name": "vpp",
  "api_version": {"major": 0, "minor": 1, "patch": 0},
  "capabilities": [
    {"type": "IFACE",     "supported": true, "max_count": 1024},
    {"type": "VLAN",      "supported": true, "max_count": 4094},
    {"type": "VRF",       "supported": true, "max_count": 4096},
    {"type": "ROUTE",     "supported": true, "max_count": 1000000,
     "features": ["ipv4", "ipv6", "multipath"]},
    {"type": "NEXTHOP",   "supported": true, "max_count": 1000000},
    {"type": "NHGROUP",   "supported": true, "max_count": 500000},
    {"type": "ACL",       "supported": true, "max_count": 16384,
     "features": ["ingress", "egress", "ipv4", "ipv6", "l4"]},
    {"type": "QOS",       "supported": true, "max_count": 1024,
     "features": ["policer-single-rate", "policer-dual-rate"]},
    {"type": "MPLS_LSP",  "supported": true, "max_count": 100000},
    {"type": "TUNNEL",    "supported": true, "max_count": 4096},
    {"type": "EVPN",      "supported": true, "max_count": 4096,
     "features": ["type2", "type3", "type5", "irb", "mh"]},
    {"type": "MULTICAST", "supported": false, "max_count": 0},
    {"type": "BFD",       "supported": true, "max_count": 1024}
  ]
}
```

### 5.2 OVS-DPDK Backend（v0.3+ 规划）

```json
{
  "name": "ovs-dpdk",
  "api_version": {"major": 0, "minor": 1, "patch": 0},
  "capabilities": [
    {"type": "IFACE",     "supported": true, "max_count": 4096},
    {"type": "VLAN",      "supported": true, "max_count": 4094},
    {"type": "VRF",       "supported": true, "max_count": 4096},
    {"type": "ROUTE",     "supported": true, "max_count": 500000,
     "features": ["ipv4", "ipv6", "multipath"]},
    {"type": "ACL",       "supported": true, "max_count": 8192,
     "features": ["ingress", "ipv4", "ipv6", "l4"]},
    {"type": "QOS",       "supported": true, "max_count": 1024,
     "features": ["policer-single-rate"]},
    {"type": "EVPN",      "supported": true, "max_count": 1024,
     "features": ["type2", "type3"]}
  ]
}
```

### 5.3 P4-DPDK Backend（v0.4+ 规划）

```json
{
  "name": "p4-dpdk",
  "api_version": {"major": 0, "minor": 1, "patch": 0},
  "capabilities": [
    {"type": "IFACE",     "supported": true, "max_count": 256},
    {"type": "ROUTE",     "supported": true, "max_count": 100000,
     "features": ["ipv4", "ipv6"]},
    {"type": "ACL",       "supported": true, "max_count": 4096,
     "features": ["ingress", "egress", "ipv4", "ipv6", "l4"]},
    {"type": "QOS",       "supported": true, "max_count": 256,
     "features": ["policer-single-rate"]}
  ]
}
```

> P4 backend 的 Capability 由 P4 程序的表规模决定，启动时从 P4Info 解析上报。

## 6. Capability 校验流程（伪代码）

```
function tx_validate(tx):
    for obj in tx.desired_objects:
        cap = danos_capability_query(tx.backend, obj.type)
        if cap == NOT_FOUND:
            return DANOS_ERR_NOT_SUPPORTED
        if not cap.supported:
            return DANOS_ERR_NOT_SUPPORTED
        if cap.max_count != 0 and current_count(obj.type) + 1 > cap.max_count:
            return DANOS_ERR_NO_CAPACITY
        for feature in obj.required_features:
            if feature not in cap.features:
                return DANOS_ERR_CAPABILITY
        if not constraints_satisfied(obj, cap.constraints_json):
            return DANOS_ERR_INVALID_ARG
    return DANOS_OK
```

## 7. 与现有实现的对齐验证

| 元素 | 代码位置 | 一致性 |
|------|---------|--------|
| `danos_capability_t` 结构 | `danos-dpa/include/danos/dpa.h:133-141` | 字段一一对应 |
| `Capability` proto message | `danos-dpa/dpa.proto:100-106` | 字段一一对应 |
| ObjectType 枚举（13 项） | `danos_dpa.h:115-130` / `dpa.proto:83-98` | 一致 |
| VPP capability 上报 | `danos-vpp/src/capability/vpp_capability.c:16-30` | §5.1 一致 |
| Core capability 校验 | `danos-core/src/capability/capability_registry.c:27-38` | §6 流程一致 |
| YANG 模型路径 | 新增 `danos-models/yang/danos-capability/` | §4 |

## 8. 后续演进（v0.2+）

- `constraints_json` → 结构化 Protobuf `google.protobuf.Any`（强类型）
- Capability diff 事件：Backend 能力变更触发受影响对象 reconcile
- Capability 协商：Client 声明所需 capability，DPA 路由到满足的 backend
- Per-backend feature flag 动态查询（如 VPP 插件加载后新增 `evpn-mh`）
