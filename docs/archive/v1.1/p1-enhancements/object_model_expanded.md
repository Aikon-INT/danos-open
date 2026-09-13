# 对象模型展开（v1.1 P1-13）

> v1.1 评审 §5.2 P1 改进动作。为 ACL / QoS / Multicast / MPLS / Tunnel
> 补充完整字段定义、生命周期、关系图。v0.1 已实现的字段标注 [v0.1]，
> v0.2+ 规划字段标注 [v0.2+]。

## 1. ACL（Access Control List）

### 1.1 字段定义

**ACL Table**（`danos_acl_table_t`，`dpa.h:432-437`）

| 字段 | 类型 | 版本 | 说明 |
|------|------|------|------|
| `table_id` | uint64 | [v0.1] | 表 ID（key） |
| `name` | string[64] | [v0.1] | 表名 |
| `bind_ifindex` | uint32 | [v0.1] | 绑定接口 |
| `ingress` | bool | [v0.1] | ingress/egress |
| `tcam_region` | string | [v0.2+] | TCAM 资源区域（硬件 backend） |
| `max_rules` | uint32 | [v0.2+] | 最大规则数（Capability 约束） |

**ACL Rule**（`danos_acl_rule_t`，`dpa.h:425-430`）

| 字段 | 类型 | 版本 | 说明 |
|------|------|------|------|
| `rule_id` | uint64 | [v0.1] | 规则 ID（key） |
| `priority` | uint32 | [v0.1] | 优先级（低=高优先级） |
| `match` | AclMatch | [v0.1] | 匹配条件 |
| `act` | AclActionParams | [v0.1] | 动作 |

**ACL Match**（`dpa.h:402-416`）

| 字段 | 类型 | 版本 | 说明 |
|------|------|------|------|
| `fields_mask` | uint32 | [v0.1] | 活动字段位图 |
| `ether_type` | uint16 | [v0.1] | 以太网类型 |
| `src_mac` / `dst_mac` | uint8[6] | [v0.1] | MAC 地址 |
| `vlan_id` | uint32 | [v0.1] | VLAN ID |
| `src_ip` / `dst_ip` | IpPrefix | [v0.1] | IP 前缀 |
| `l4_proto` | uint8 | [v0.1] | L4 协议（6=TCP, 17=UDP） |
| `l4_src_port_start/end` | uint16 | [v0.1] | L4 源端口范围 |
| `l4_dst_port_start/end` | uint16 | [v0.1] | L4 目的端口范围 |
| `dscp` | uint8 | [v0.1] | DSCP |
| `mpls_label` | uint32 | [v0.2+] | MPLS 标签匹配 |
| `tunnel_id` | uint32 | [v0.2+] | 隧道 ID 匹配 |

**ACL Action**（`dpa.h:393-400`）

| 动作 | 版本 | 参数 |
|------|------|------|
| PERMIT | [v0.1] | 无 |
| DENY | [v0.1] | 无 |
| MIRROR | [v0.1] | `redirect_ifindex`（镜像目标） |
| REDIRECT | [v0.1] | `redirect_ifindex` |
| POLICE | [v0.1] | `police_rate_kbps` |
| SET_DSCP | [v0.1] | `set_dscp` |
| COUNT | [v0.2+] | 命中计数（无动作，仅统计） |
| LOG | [v0.2+] | 日志记录命中 |

### 1.2 生命周期

```
CREATE → ACTIVE → UPDATE → ACTIVE
                → DELETE → REMOVED
```

### 1.3 TCAM 资源声明（v0.2+）

```json
{
  "tcam_region": "acl-ipv4",
  "max_rules": 16384,
  "entry_width_bits": 160,
  "key_pattern": "src_ip(32)+dst_ip(32)+l4_proto(8)+src_port(16)+dst_port(16)"
}
```

### 1.4 关系图

```
ACL Table ──bind──→ Interface
    │
    └──contains──→ ACL Rule ──match──→ Packet Fields
                  ──action──→ {Permit, Deny, Mirror, Redirect, Police, SetDSCP}
```

## 2. QoS

### 2.1 字段定义

**QoS Policy**（`danos_qos_policy_t`，`dpa.h:448-459`）

| 字段 | 类型 | 版本 | 说明 |
|------|------|------|------|
| `policy_id` | uint64 | [v0.1] | 策略 ID（key） |
| `name` | string[64] | [v0.1] | 策略名 |
| `cir_bps` | uint64 | [v0.1] | CIR（承诺信息速率） |
| `cb_bytes` | uint64 | [v0.1] | 承诺突发 |
| `pir_bps` | uint64 | [v0.1] | PIR（0=单速率） |
| `pb_bytes` | uint64 | [v0.1] | 峰值突发 |
| `conform_dscp` | uint8 | [v0.1] | 符合标记 |
| `exceed_dscp` | uint8 | [v0.1] | 超出标记 |
| `violate_dscp` | uint8 | [v0.1] | 违反标记 |
| `level` | enum | [v0.2+] | 层次：port/queue/class |
| `scheduler` | enum | [v0.2+] | 调度器：DWRR/SP/WFQ |
| `shaper_type` | enum | [v0.2+] | 整形器：single-rate/dual-rate |
| `marker` | enum | [v0.2+] | 标记器：DSCP/802.1p |

### 2.2 QoS 层次（v0.2+ HQoS）

```
Port Level
  └── Queue Level (4-8 queues per port)
       └── Class Level (per-flow classification)
```

| 层次 | v0.1 | v0.2+ |
|------|------|-------|
| Port（端口整形） | ✓（policer） | ✓ |
| Queue（队列调度） | - | DWRR/SP/WFQ |
| Class（流分类） | - | 基于 ACL match |

### 2.3 Capability 区分

| Capability | v0.1（flat QoS） | v0.2+（HQoS） |
|-----------|-----------------|---------------|
| `policer-single-rate` | ✓ | ✓ |
| `policer-dual-rate` | ✓ | ✓ |
| `scheduler-dwrr` | - | ✓ |
| `scheduler-sp` | - | ✓ |
| `scheduler-wfq` | - | ✓ |
| `hqos` | - | ✓ |

### 2.4 关系图

```
QoS Policy ──bind──→ Interface (ingress/egress)
    │
    ├──policer──→ {single-rate, dual-rate}
    └──scheduler──→ Queue ────→ Class ──→ ACL match
```

## 3. Multicast（v0.2+）

### 3.1 字段定义

**Multicast Route**（`danos_mroute_t`，`dpa.h:523-530`）

| 字段 | 类型 | 说明 |
|------|------|------|
| `vrf_id` | uint32 | VRF ID |
| `source` | IpAddr | S for (S,G); 0 for (*,G) |
| `group` | IpAddr | 组地址 G |
| `incoming_if` | obj_id | RPF 接口 |
| `oif_count` | uint32 | 出接口数 |
| `oif_list[64]` | ifindex[] | 出接口列表 |

### 3.2 协议支持

| 协议 | RFC | 说明 |
|------|-----|------|
| PIM-SM | RFC 7761 | (S,G) 和 (*,G) 状态 |
| IGMPv3 | RFC 3376 | IPv4 组成员 |
| MLDv2 | RFC 3810 | IPv6 组成员 |
| BGP-MVPN | RFC 6514 | 跨域组播 |

### 3.3 数据面 replication

```
(S,G) → RPF 接口入 → 复制到 oif_list[] → 各出接口转发
```

VPP: `ip4_mfib_signal` / `ip6_mfib_signal`
P4: multicast group table + replication engine

## 4. MPLS / LSP（v0.2+）

### 4.1 字段定义

**MPLS LSP**（`danos_mpls_lsp_t`，`dpa.h:476-483`）

| 字段 | 类型 | 说明 |
|------|------|------|
| `in_label` | uint32 | 入标签（key，20 bits） |
| `type` | enum | LDP/RSVP-TE/SR/STATIC |
| `nhgroup_id` | obj_id | 转发 NHGroup |
| `php` | bool | 倒数第二跳弹出 |
| `push_label_count` | uint8 | 压入标签数（0-3） |
| `push_labels[3]` | uint32[] | 压入标签栈 |

### 4.2 信令类型

| 类型 | 枚举 | 说明 |
|------|------|------|
| LDP | `DANOS_MPLS_TYPE_LDP` | LDP 信令 |
| RSVP-TE | `DANOS_MPLS_TYPE_RSVP_TE` | RSVP-TE 信令 |
| SR | `DANOS_MPLS_TYPE_SR` | Segment Routing（RFC 8660） |
| STATIC | `DANOS_MPLS_TYPE_STATIC` | 静态 LSP |

### 4.3 操作语义

| 场景 | 操作 |
|------|------|
| PHP | 倒数第二跳弹出标签，查 IP FIB |
| Explicit pop | 弹出标签，查对应 LSP |
| Push | 压入标签栈，转发到 NHGroup |
| Swap | 弹出入标签，压入 push_labels |

## 5. Tunnel（v0.2+）

### 5.1 字段定义

**Tunnel**（`danos_tunnel_t`，`dpa.h:496-504`）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | obj_id | 隧道 ID（key） |
| `type` | enum | VXLAN/GRE/IPIP/SR-TE |
| `ifindex` | uint32 | 隧道接口 |
| `src` | IpAddr | 源地址 |
| `dst` | IpAddr | 目的地址 |
| `vni` | uint32 | VXLAN VNI（24 bits） |
| `gre_key` | uint32 | GRE key |

### 5.2 子类型

| 类型 | 枚举 | encap | decap |
|------|------|-------|-------|
| VXLAN | `DANOS_TUNNEL_VXLAN` | UDP/4789 + VXLAN header | 反之 |
| GRE | `DANOS_TUNNEL_GRE` | IP + GRE header | 反之 |
| IPIP | `DANOS_TUNNEL_IPIP` | IP-in-IP | 反之 |
| SR-TE | `DANOS_TUNNEL_SR_TE` | MPLS label stack（RFC 8667） | 反之 |
| IPsec | [v0.3+] | ESP 加密 | 解密 |

### 5.3 统一抽象

所有隧道类型共享 `danos_tunnel_t` 结构，差异通过 `type` 字段区分。
encap/decap 属性由 Backend 根据 type 实现。

### 5.4 关系图

```
Tunnel ──ifindex──→ Interface (type=VXLAN/GRE)
      ──src/dst──→ IP Address
      ──vni──→ VXLAN VNI (for VXLAN)
      ──gre_key──→ GRE Key (for GRE)
```

## 6. 与现有代码的对齐

| 对象 | C ABI 位置 | YANG 位置 | Protobuf 位置 |
|------|-----------|----------|--------------|
| ACL | `dpa.h:380-442` | `danos-acl.yang` | `dpa.proto:244-288` |
| QoS | `dpa.h:445-463` | `danos-qos.yang` | `dpa.proto:294-304` |
| MPLS LSP | `dpa.h:466-483` | 待补（v0.2+） | 待补 |
| Tunnel | `dpa.h:486-504` | 待补（v0.2+） | 待补 |
| Multicast | `dpa.h:519-530` | 待补（v0.2+） | 待补 |

**v0.2+ 待补 YANG**：`danos-mpls`、`danos-tunnel`、`danos-multicast` 模块
