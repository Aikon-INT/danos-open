# DANOS-Open Architecture Specification v1.1

> **版本**：v1.1（基于 v1.0 评审报告修订）
> **状态**：可指导 v0.1 实施
> **修订日期**：2026-09-05
> **许可**：Apache-2.0
>
> **v1.1 相对 v1.0 的变更**：
> - 修复仓库结构不一致（§13 重写）
> - 新增 DPA API Specification（§7A-7D，含 C ABI + Protobuf + 错误码 + 版本协商）
> - 形式化 Capability 与 DPA Object 关系（§9 重写）
> - 新增安全架构（§20）
> - 新增 HA 设计（§21）
> - 新增可观测性与 Telemetry（§22）
> - 新增风险评估与缓解（§26）
> - 补全 MVP v0.1 DoD（§14A）
> - 明确 P4 子项关系（§17 修订）
> - MVP 与实施计划时间线对齐（§18 修订）

---

## 目录

1. 项目背景与总体目标
2. DANOS 2018 架构设想
3. DANOS-Open vs OcNOS Feature Gap 核心结论
4. 重点功能的开源项目借鉴
5. DANOS-Open v1.0 Architecture Specification（四条原则）
6. 总体架构
7. DPA Object Model
8. EVPN Object
9. Capability Model（v1.1 重写）
10. Transaction Model
11. State & Reconciliation
12. FRR → DPA → Dataplane
13. Repository Architecture（v1.1 重写）
14. MVP v0.1–v0.4
14A. MVP v0.1 Definition of Done（v1.1 新增）
15. 测试架构
16. OcNOS Compatibility Layer
17. v1.0 架构决策冻结（v1.1 修订 P4 子项）
18. 下一阶段实施计划（v1.1 修订时间线对齐）
19. 项目最终定位
20. 安全架构（v1.1 新增）
21. 高可用 HA 设计（v1.1 新增）
22. 可观测性与 Telemetry（v1.1 新增）
26. 风险评估与缓解（v1.1 新增）

**附录**：
- A. DPA API Specification v0.1（C ABI + Protobuf + 语义规范）
- B. FRR ZAPI → DPA 映射表
- C. 依赖版本兼容矩阵
- D. RFC 对齐表

---

## 1. 项目背景与总体目标

DANOS-Open 重新实现早期 DANOS 的核心思想：利用 Linux、FRR、VPP、OVS、DPDK、P4 等开源软件构建 Software-First、ASIC-SDK-Free 的 NOS。核心不是重新实现 BGP、OSPF、IS-IS、PIM、BFD 等成熟协议，而是通过统一 Data Plane Abstraction（DPA）、状态模型、事务机制、Capability Discovery 与 Reconciliation，将控制平面、管理平面和不同数据平面解耦。

## 2. DANOS 2018 架构设想

2018 年 DANOS 设想包含 Autonomous NOS、AT&T dNOS seed code、FRR、OpenSwitch/SONiC SAI，以及随着社区成熟逐步集成 P4/Stratum。DANOS-Open 的重构方向是：DPA 替代 SAI 成为通用数据平面抽象；生产软件数据平面优先 VPP+DPDK；FRR 负责成熟协议；EVPN 优先于 VPLS/VPWS，SR-TE 优先于 RSVP-TE，EVPN-MH 优先于 ICCP。

## 3. DANOS-Open vs OcNOS Feature Gap 核心结论

PIM-DM：FRR 可直接复用，优先级高。EVPN：FRR BGP EVPN + VPP/OVS，作为现代 L2VPN 核心，P0。VPLS/VPWS：FRR LDP 控制面能力需谨慎，freeRouter 作为行为参考，VPP/OVS 负责数据面，P2。Private VLAN：成熟完整开源实现较少，可用 Linux bridge、OVS、VPP 规则近似，P2。RSVP-TE：freeRouter 可作为协议参考，长期优先 SR-TE，P3。ICCP：成熟开源实现极少，优先 EVPN-MH，P3。HQoS：VPP QoS / DPDK rte_tm/rte_mtr 可作为基础，但软件数据面应限定预期，P1。

## 4. 重点功能的开源项目借鉴

Private VLAN：Linux bridge + VLAN filtering、OVS OpenFlow/端口隔离、VPP 分类/ACL 规则可作为基础，不追求商业设备 100% 兼容。RSVP-TE：freeRouter/freeRtr 适合协议逻辑与行为参考；DANOS-Open 长期应优先 SR-TE。PIM-DM：直接复用 FRR pimd。ICCP：不宜作为核心依赖；若必须兼容，独立模块化实现。VPLS/VPWS：FRR LDP 作为控制面参考，freeRouter 作为协议行为参考，VPP/OVS 负责实际转发；长期 EVPN 替代。

## 5. 四条不可违反的原则

1. **No Proprietary SDK Dependency**：Core 不依赖 Broadcom/Marvell/NVIDIA 等闭源 ASIC SDK，也不以厂商 SAI 为核心。
2. **Do Not Reimplement Mature Protocols**：BGP、OSPF、IS-IS、PIM、BFD、LDP 等由 FRR 提供。
3. **DPA ≠ SAI**：DPA 是 Capability-based Data Plane API，不假设 ASIC、fixed pipeline 或 vendor SDK。
4. **EVPN/SR 优先于 Legacy VPN/TE**：EVPN > VPLS；SR-TE > RSVP-TE；EVPN-MH > ICCP。此原则写入 Compatibility Layer：**兼容 ≠ 优先实现**。

## 6. 总体架构

```
Management：CLI / NETCONF / RESTCONF / gNMI / OpenConfig / YANG
                    ↓
        Desired / Programmed / Oper State
                    ↓
            Transaction / Reconciliation
                    ↓
DANOS DPA：Route / NH / NHGroup / VRF / ACL / QoS / MPLS / Tunnel / EVPN / Multicast
                    ↓
            VPP / OVS-DPDK / P4-DPDK
                    ↓
        Linux / DPDK / NIC / VM / FPGA
```

Control Plane：FRR（BGP、OSPF/OSPFv3、IS-IS、BFD、PIM、LDP、EVPN 等）。FRR 通过 ZAPI/FIB 事件与 DANOS FIB Adapter 交互，不直接依赖具体 dataplane。

## 7. DPA Object Model

第一版对象：Interface、VLAN/QinQ、VRF、Route、NextHop、NHGroup、ACL、QoS、MPLS/LSP、Tunnel、EVPN、Multicast。

- Route = VRF + Prefix + Protocol + Admin Distance + Metric + NHGroup
- NextHop = Gateway + Interface + Weight + Flags
- NHGroup = 多个 NextHop 的集合，用于 ECMP、weighted ECMP、resilient hashing、BGP multipath

> **v1.1 注**：完整 C ABI 定义见附录 A `danos_dpa.h`；完整 Protobuf IDL 见附录 A `danos_dpa.proto`；API 语义规范（错误码、版本、事务、并发）见附录 A `danos_dpa_spec.md`。

## 8. EVPN Object

EVPN Instance 至少包含 EVI、RD、RT import/export、VNI、MAC/IP routes、Ethernet Segment、Multihoming。控制面使用 FRR BGP EVPN，数据面可映射到 VPP VXLAN、OVS VXLAN 或 P4 match-action。

> **v1.1 注**：对齐 RFC 7432、8365、9161。EVPN-MH 对齐 RFC 7432 §8。

## 9. Capability Model（v1.1 重写）

### 9.1 形式化定义

```
Capability: ObjectType → {
    supported: bool,
    max_count: uint32,
    features: [string],
    constraints: JSON
}
```

- Capability 是 DPA Object 的**元数据**，不是独立对象
- 每个 ObjectType 对应一组 Capability 声明
- Backend 启动时上报 Capability Profile
- DPA Core 在 PREPARE/VALIDATE 阶段校验 Desired State 是否在 Capability 范围内

### 9.2 校验时机

1. **Backend 注册时**：Core 记录所有 backend 的 Capability Profile
2. **事务 VALIDATE 阶段**：Core 检查每个 op 是否被目标 backend 的 Capability 支持
3. **运行时 Capability 变更**：Backend 通过 `EVENT_CAPABILITY` 通知 Core，Core 重新评估

### 9.3 示例

VPP backend 上报：
```json
{
  "OBJ_ROUTE":    {"supported": true, "max_count": 1000000, "features": ["ipv4", "ipv6", "multipath"]},
  "OBJ_EVPN":     {"supported": true, "max_count": 4096,    "features": ["type2", "type3", "type5", "irb", "mh"]},
  "OBJ_QOS":      {"supported": true, "max_count": 1024,    "features": ["policer-single-rate"]},
  "OBJ_MULTICAST":{"supported": false}
}
```

## 10. Transaction Model

事务流程：OPEN → PREPARE → VALIDATE → COMMIT → VERIFY → DONE；错误时 ABORT 或 ROLLBACK。

API：`dpa_tx_begin() / dpa_tx_prepare() / dpa_tx_validate() / dpa_tx_commit() / dpa_tx_abort() / dpa_tx_rollback()`。每次变更使用 uint64_t Transaction ID，服务于日志、HA、rollback、telemetry、debugging 与 audit。

> **v1.1 补充**：
> - **并发控制**：多读单写（读无锁并发，写全局有序）
> - **隔离级别**：Read Committed + 写事务原子可见
> - **超时**：PREPARE 100ms / COMMIT 500ms / VERIFY 1000ms（可配置）
> - **嵌套**：不支持嵌套；子操作扁平化为单事务多个 op
> - **持久化**：WAL（Write-Ahead Log），用于 HA 同步与审计
> - **回滚**：程序化回滚（inverse op）+ 状态重编程（fallback）

## 11. State & Reconciliation

状态模型：CONFIG → DESIRED → PROGRAMMED → OPER。danos-reconciler 比较 Desired 与 Programmed/Oper；发现 dataplane 漂移后自动重新执行必要的 program/repair 操作。

> **v1.1 补充**：
> - **触发**：事件驱动（Oper 变更通知）+ 周期对账（默认 30s）
> - **算法**：diff → 拓扑序重编程 → 指数退避重试（1s, 2s, 4s, ..., 上限 60s）→ 连续失败告警
> - **防震荡**：同一对象 5s 内最多 3 次，超过冻结并告警
> - **收敛性**：单调递增 TxID + 最终一致（假设 backend 最终响应）

## 12. FRR → DPA → Dataplane

FRR bgpd/ospfd/isisd/bfdd/pimd/ldpd → zebra → ZAPI → DANOS FIB Adapter → normalized DPA Object → Transaction → Backend Dispatcher → VPP / OVS / P4。FRR 不直接调用 VPP/OVS/P4；FIB Adapter 负责控制面事件归一化。

> **v1.1 注**：ZAPI → DPA 完整映射表见附录 B。FRR 以独立进程运行，通过 ZAPI socket 通信，不链接 FRR 库（GPLv2 隔离）。

## 13. Repository Architecture（v1.1 重写）

> 修复 v1.0 仓库结构与正文模块引用不一致的硬伤。

```
danos-open/
├── danos-core/            # 核心引擎：object/state/transaction/capability/event
├── danos-dpa/             # DPA 公共 API（C ABI + Protobuf IDL + 错误码 + 版本协商）
├── danos-fib/             # FRR zebra/FIB adapter（ZAPI → DPA 映射）
├── danos-vpp/             # VPP backend 适配
├── danos-ovs/             # OVS-DPDK backend 适配
├── danos-p4/              # P4Runtime / P4 backend 适配
├── danos-models/          # YANG / OpenConfig 模型
├── danos-mgmt/            # 管理面：CLI / gNMI / NETCONF / RESTCONF
├── danos-compat/          # OcNOS 兼容层（OcNOS-like CLI 与语义映射）
├── danos-ha/              # 高可用：NSR / VRRP / EVPN-MH / supervisor / ISSU
├── danos-observability/   # 可观测性：telemetry / prometheus / tracing / audit
├── danos-security/        # 安全：AAA / TLS / key management / CoPP
├── danos-platform/        # 平台适配：x86 / ARM / generic
├── danos-test/            # 测试：topology / integration / scale / traffic / perf / conformance
├── danos-build/           # 构建：Debian / Ubuntu / container / OCI image / SBOM
├── danos-docs/            # 文档：architecture / RFC / API spec / runbook / ADR
├── CONTRIBUTING.md
├── LICENSE                # Apache-2.0
└── NOTICE
```

> 完整模块职责说明与依赖关系图见 `v1.1/repo-structure/repository_structure_v1.1.md`。

**关键依赖规则**：
1. `danos-core` 不依赖任何 backend
2. backend 不依赖彼此
3. `danos-fib` 通过 gRPC 与 core 通信，不直接链接 FRR
4. `danos-compat` 不直接操作 backend，必须经 core
5. 所有模块依赖 `danos-dpa` 的 API 定义

## 14. MVP v0.1–v0.4

- **v0.1**：L2 Ethernet/VLAN/QinQ/Bridge/LACP；L3 IPv4/IPv6/VRF/Static/BGP/OSPF/IS-IS/BFD；基础 ACL/QoS；YANG/NETCONF/CLI；仅 VPP+DPDK
- **v0.2**：PIM/IGMP/MLD/MPLS/LDP/VXLAN/EVPN
- **v0.3**：加入 OVS-DPDK backend，验证同一 DPA Object 在不同 backend 的一致语义
- **v0.4**：加入 P4Runtime、P4C、BMv2、P4-DPDK；BMv2 主要用于测试

## 14A. MVP v0.1 Definition of Done（v1.1 新增）

> 完整可勾选清单见 `v1.1/mvp/mvp_v0.1_dod.md`，共 44 项 P0 + 6 项 P1。

**v0.1 发布门槛**：44 项 P0 全部完成 + 退出条件满足（无 P0 bug、CI 连续 7 天绿灯、性能基线达标、conformance 100% 通过）。

## 15. 测试架构

第一天开始测试：复用 FRR topotest，结合 VPP unit/scale tests、Containerlab、freeRouter/FRR 互通。

- 基础三节点：BGP / OSPF / IS-IS / BFD / ECMP / VRF / ACL
- EVPN Leaf-Spine：EVPN / VXLAN / MAC learning / ARP suppression / IRB / L2VNI / L3VNI
- MPLS：LDP / MPLS / LSP / VPNv4 / VPNv6 / EVPN-MPLS / SR-MPLS
- Scale：1K / 10K / 100K / 1M 路由与对象
- **Conformance**（v1.1 新增）：DPA API conformance，所有 backend 必过
- **Consistency**（v1.1 新增）：多 backend 语义一致性

## 16. OcNOS Compatibility Layer

建立 `danos-compat/`，负责 OcNOS-like CLI、旧配置语义和兼容行为映射。Compatibility Layer → DANOS Model → DPA，而不是让 OcNOS 风格直接操作 VPP。

> **v1.1 注**：兼容 ≠ 优先实现。EVPN/SR 优先原则不变，兼容层仅提供 legacy 接口。

## 17. v1.0 架构决策冻结（v1.1 修订 P4 子项）

| 决策 | 值 |
|------|-----|
| NOS Kernel | Linux |
| Routing Control Plane | FRR |
| Primary Dataplane | VPP |
| Packet acceleration | DPDK |
| Secondary dataplane | OVS-DPDK |
| Programmable dataplane | P4-DPDK |
| P4 test target | BMv2（仅测试，不用于生产） |
| P4 toolchain | P4C（编译器，非运行时） |
| P4 control protocol | P4Runtime |
| Config model | YANG / OpenConfig |
| Northbound | NETCONF + gNMI |
| Internal abstraction | DANOS DPA |
| State | Desired / Programmed / Oper |
| Transactions | Prepare / Validate / Commit / Rollback |
| Backend discovery | Capability API |
| L2VPN primary | EVPN |
| Legacy L2VPN | VPLS/VPWS later |
| TE primary | SR-TE |
| Legacy TE | RSVP-TE later |
| HA primary | EVPN-MH / BFD / VRRP |
| ICCP | Optional / P3 |
| ASIC SDK | Forbidden in Core |
| SAI | Not a dependency |

**P4 子项关系澄清**（v1.1）：
- P4C：编译器，工具链依赖，非运行时
- BMv2：软件 P4 target，仅用于功能测试与 CI，**不用于生产**
- P4-DPDK：生产 P4 target，基于 DPDK 的 P4 软件 dataplane
- P4Runtime：控制面协议，DANOS DPA → P4Runtime Adapter → target
- 优先级：P4Runtime + BMv2（v0.4 功能验证）→ P4-DPDK（v0.5+ 生产）

## 18. 下一阶段实施计划（v1.1 修订时间线对齐）

| 步骤 | 内容 | 对应 MVP | 验收标准 |
|------|------|---------|---------|
| 1 | DPA Object Model 定义 | v0.1 前置 | 所有对象 YAML/Protobuf schema 评审通过 |
| 2 | DPA API Spec v0.1 发布 | v0.1 前置 | C ABI + Protobuf + 错误码 + 版本号 |
| 3 | FRR Zebra/FIB Adapter | v0.1 | ZAPI 事件 → DPA 对象映射表完成 |
| 4 | VPP Backend 基础对象 | v0.1 | Route/NH/VRF/IF 通过单元测试 |
| 5 | x86/ARM + VPP + DPDK CI | v0.1 | CI 绿灯，镜像可构建 |
| 6 | BGP/OSPF/IS-IS/BFD E2E | v0.1 DoD | 三节点拓扑全协议通过 |
| 7 | EVPN/VXLAN | v0.2 DoD | Leaf-Spine EVPN 全场景通过 |
| 8 | OVS-DPDK backend | v0.3 DoD | 同一 DPA 对象双 backend 语义一致 |
| 9 | P4Runtime + BMv2 | v0.4 DoD | P4 程序加载、表项下发通过 |

## 19. 项目最终定位

DANOS-Open 不是"没有 ASIC SDK 的 SONiC"，而是"以成熟开源积木 + DPA 形成真正解耦的 NOS"。真正需要自研并形成项目核心 IP 的部分，应集中在统一网络状态模型、DPA、Capability、Transaction、Reconciliation、FRR FIB Adapter，以及多个开源 dataplane backend 的集成，而不是重复实现成熟协议栈。

---

## 20. 安全架构（v1.1 新增）

> 完整内容见 `v1.1/security/security_architecture.md`。

**要点**：
- 管理面认证：SSH public key + AAA（TACACS+/RADIUS）+ mTLS
- 授权：RBAC（admin/operator/viewer/security-admin）
- 传输安全：TLS 1.3 强制、SSH 2.0
- 控制面安全：BGP GTSM + TCP-AO + RPKI、OSPF/IS-IS 认证
- CoPP：控制面流量分类限速
- 默认拒绝：ACL deny all、接口 admin-down
- 密钥管理：KMS 集成、轮换
- 审计：Transaction ID 链、append-only、远程同步

**MVP 对齐**：SSH/RBAC/TLS/BGP TTL/审计进入 v0.1；CoPP/TCP-AO/RPKI/KMS 进入 v0.2。

## 21. 高可用 HA 设计（v1.1 新增）

> 完整内容见 `v1.1/ha/ha_design.md`。

**要点**：
- 进程级：supervisor + 重启退避 + 热补丁
- 协议级 NSR：FRR nsr 集成，切换无感知
- 链路级：BFD（单跳 900ms / 多跳 3s）+ LACP 联动
- 网关级：VRRP + EVPN-MH（RFC 7432 §8）
- 节点级：双路由引擎 + 状态同步 + 切换 < 30s
- 软件级：ISSU 滚动升级 + 回滚

**MVP 对齐**：supervisor/BFD 单跳/LACP 进入 v0.1；VRRP/EVPN-MH/BFD 多跳进入 v0.2；NSR/双引擎/ISSU 进入 v0.5+。

## 22. 可观测性与 Telemetry（v1.1 新增）

> 完整内容见 `v1.1/observability/observability_telemetry.md`。

**要点**：
- Metrics：Prometheus exporter + 50+ 核心 metric + Grafana dashboard + Alertmanager
- Logs：结构化 JSON + 集中收集（Loki/ELK）+ 敏感信息脱敏
- Traces：OpenTelemetry 跨 gNMI→Core→Backend
- 流式 Telemetry：gNMI Subscribe（ON_CHANGE/SAMPLE）+ IPFIX
- 审计：见 §20.9

**MVP 对齐**：Prometheus/JSON 日志/审计进入 v0.1；gNMI Subscribe/dashboard/告警进入 v0.2；OpenTelemetry/IPFIX 进入 v0.5+。

## 26. 风险评估与缓解（v1.1 新增）

> 完整内容见 `v1.1/risk/risk_assessment.md`，含 12 项风险登记册。

**风险摘要**：
| ID | 风险 | 等级 | 缓解 |
|----|------|------|------|
| R-T-01 | 多 backend 语义不一致 | 高 | conformance + consistency 测试 |
| R-P-01 | 软件 dataplane 性能 | 高 | 场景限定 + 性能基线 |
| R-T-02 | FRR ZAPI 变更 | 中 | 版本锁定 + mock 测试 |
| R-T-03 | DPDK ABI 破坏 | 中 | LTS + 容器隔离 |
| R-E-01 | FRR 社区 | 中 | 跟主线 + 内部 fork |
| R-L-01 | GPLv2 传染 | 中 | 进程隔离 + 法务确认 |

---

## 附录 A. DPA API Specification v0.1

| 文件 | 说明 |
|------|------|
| `v1.1/dpa-spec/danos_dpa.h` | C ABI 头文件（完整，含所有对象 CRUD + 事务 + Capability + 事件 + Reconcile） |
| `v1.1/dpa-spec/danos_dpa.proto` | Protobuf IDL + gRPC service（与 C ABI 等价） |
| `v1.1/dpa-spec/danos_dpa_spec.md` | API 语义规范（错误码 21 个、版本协商、事务状态机、并发模型、Reconcile 策略、Capability 关系、ABI 稳定性承诺） |

## 附录 B. FRR ZAPI → DPA 映射表（P1，v0.2 前完成）

| ZAPI 消息 | DPA 对象 | DPA 操作 |
|-----------|---------|---------|
| ZEBRA_ROUTE_ADD | Route + NHGroup | create/update |
| ZEBRA_ROUTE_DELETE | Route | delete |
| ZEBRA_NEXTHOP_LOOKUP | NH / NHGroup | read |
| ZEBRA_INTERFACE_ADD | Interface | create |
| ZEBRA_INTERFACE_SET_MTU | Interface | update |
| ZEBRA_LABELS_ADD | MPLS LSP | create |
| ZEBRA_BFD_DEST_REGISTER | BFD Session | create |

## 附录 C. 依赖版本兼容矩阵

| DANOS-Open | FRR | VPP | DPDK | OVS | P4Runtime | Linux |
|-----------|-----|-----|------|-----|-----------|-------|
| v0.1 | 10.x | 24.x | 23.x | - | - | 6.6+ |
| v0.2 | 10.x | 24.x | 23.x | - | - | 6.6+ |
| v0.3 | 10.x | 24.x | 23.x | 3.x | - | 6.6+ |
| v0.4 | 10.x | 24.x | 23.x | 3.x | 1.x | 6.6+ |

## 附录 D. RFC 对齐表

| 功能 | RFC |
|------|-----|
| EVPN | 7432, 8365, 9161 |
| EVPN-MH | 7432 §8 |
| VRRP | 5798 |
| BFD | 5880, 5881, 5883 |
| PIM-SM | 7761 |
| IGMPv3 | 3376 |
| MLDv2 | 3810 |
| SR-MPLS | 8660 |
| SR-TE | 8667 |
| NETCONF | 6241 |
| TCP-AO | 5925 |
| BGP GR | 4724 |

---

## v1.1 修订总结

| 维度 | v1.0 评级 | v1.1 改进 | v1.1 评级 |
|------|----------|----------|----------|
| 完整性 | ★★★☆☆ | +安全/HA/可观测性/风险四章 | ★★★★★ |
| 一致性 | ★★★☆☆ | 仓库结构修复、P4 关系澄清、时间线对齐 | ★★★★★ |
| 可实施性 | ★★☆☆☆ | +完整 API Spec/错误码/版本/DoD | ★★★★☆ |
| 技术深度 | ★★★☆☆ | +Capability 形式化/事务并发模型/Reconcile 算法 | ★★★★☆ |
| 风险评估 | ★☆☆☆☆ | +12 项风险登记册 | ★★★★☆ |

**v1.1 状态**：可指导 v0.1 实施的架构规格说明书。

---

*DANOS-Open Architecture Specification v1.1 — 2026-09-05 — Apache-2.0*
