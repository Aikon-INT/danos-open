# DANOS-Open 架构文档综合评估与完善建议方案

> 评审对象：`DANOS-Open_Architecture_Feature-Gap_v1.0.docx`（v1.0，共 19 章）
> 评审版本：v1.1 评审与增强建议
> 评审维度：完整性 / 一致性 / 可实施性 / 技术深度 / 风险评估 / 路线图合理性

---

## 一、总体评价

v1.0 文档在**架构原则、DPA 抽象、FRR 集成路径、MVP 路线、仓库结构**等方面已建立清晰骨架，四条不可违反原则（No Proprietary SDK / Do Not Reimplement / DPA ≠ SAI / EVPN & SR 优先）立场明确，定位准确。文档作为"对话技术内容汇编"已具备 v1.0 架构规格的雏形。

但作为可指导工程实施的**架构规格说明书**，存在以下系统性短板：

| 维度 | 评级 | 主要问题 |
|------|------|----------|
| 完整性 | ★★★☆☆ | 缺少安全、HA、可观测性、生命周期、风险等关键章节 |
| 一致性 | ★★★☆☆ | 仓库结构与正文模块引用不一致；P4 子项关系未澄清 |
| 可实施性 | ★★☆☆☆ | API 仅片段；契约、错误码、版本、验收标准缺失 |
| 技术深度 | ★★★☆☆ | 对象模型未展开；与 RFC/标准对齐缺失 |
| 风险评估 | ★☆☆☆☆ | 完全缺失 |
| 路线图 | ★★★☆☆ | MVP 与实施计划时间线对应关系不清 |

---

## 二、完整性缺口与补全建议

### 2.1 缺失章节清单（建议新增 7 章）

| 建议章节 | 标题 | 优先级 | 建议内容要点 |
|---------|------|--------|-------------|
| 20 | **安全架构** | P0 | ① 管理面认证：NETCONF/gNMI over TLS、SSH、mTLS、证书轮换；② 授权：RBAC + AAA（TACACS+/RADIUS）；③ 审计：所有 Transaction ID 记录操作主体、时间、变更 diff；④ 控制面安全：BGP TTL Security、GTSM、TCP-AO、Keychain；⑤ 数据面安全：ACL 默认 deny、Control Plane Policing (CoPP)；⑥ 密钥管理：集中式 KMS、密钥轮换策略 |
| 21 | **高可用 HA 设计** | P0 | ① NSR/Graceful Restart 与 FRR `nsr` 集成；② VRRP/M-VRRP 多主网关；③ EVPN-MH（RFC 7432 §8）ESI 自动化、Alias/Per-EVI AD；④ BFD 多跳/单跳、微秒级；⑤ 进程级 HA：supervisor、进程重启策略、热补丁；⑥ 节点级 HA：双路由引擎、状态同步、ISSU |
| 22 | **可观测性与 Telemetry** | P0 | ① gNMI Subscribe（STREAM/ON_CHANGE/SAMPLE）；② OpenConfig telemetry paths；③ Prometheus exporter + Grafana dashboard；④ 流式 telemetry：gRPC stream、IPFIX/NetFlow；⑤ 结构化日志（JSON）、集中收集（Loki/ELK）；⑥ 分布式追踪（OpenTelemetry）跨 FRR→Adapter→DPA→Backend；⑦ 告警：Alertmanager 规则、Runbook |
| 23 | **生命周期与升级管理** | P1 | ① 滚动升级：节点级 cordon/drain、控制面收敛后再切换数据面；② A/B 部署：双 dataplane 并行、流量切换；③ 配置回滚：Transaction ID 链式回滚；④ 镜像管理：OCI 镜像、签名（cosign）、SBOM；⑤ 兼容性：DPA API 版本协商、YANG 模型版本 |
| 24 | **部署与编排** | P1 | ① 单节点裸机部署（Debian/Ubuntu 包）；② 容器化部署（Docker/K8s DaemonSet）；③ 多节点集群：控制面分布式（FRR 多实例）、数据面分布式；④ 网络编排：与 K8s CNI、Multus 集成；⑤ 配置管理：Ansible/Terraform Provider |
| 25 | **许可证与社区治理** | P1 | ① 许可证选择：Apache-2.0 推荐（与 FRR GPLv2、VPP Apache-2.0、DPDK BSD 兼容性矩阵）；② CLA/DCO 策略；③ 贡献流程：RFC 流程、PR 评审、CI 门槛；④ 商标与品牌策略；⑤ 与 LF Networking / ONF 关系 |
| 26 | **风险评估与缓解** | P0 | 见第六节 |

### 2.2 现有章节补强建议

- **第 7 节 DPA Object Model**：补全所有对象的字段定义、生命周期、关系图（UML/ER 图）
- **第 9 节 Capability Model**：明确 Schema 格式（建议 YANG + Protobuf 双发），给出完整示例
- **第 10 节 Transaction Model**：补充并发控制、隔离级别、超时、嵌套事务策略
- **第 11 节 State & Reconciliation**：补充算法策略（事件驱动 + 周期对账 + 指数退避）、收敛性证明
- **第 14 节 MVP**：每版本补充 Definition of Done（DoD）与退出条件
- **第 15 节 测试架构**：补充性能基线、覆盖率门槛、CI/CD 流水线设计

---

## 三、一致性问题与修复建议

### 3.1 仓库结构不一致（必须修复）

**问题**：第 13 节仓库结构缺少第 16 节 `danos-compat/`、HA 模块、可观测性模块。

**建议修订后的仓库结构**：

```
danos-open/
├── danos-core/           # object/state/transaction/capability/event
├── danos-dpa/            # DPA public API (C ABI + Protobuf IDL)
├── danos-fib/            # FRR zebra/FIB adapter
├── danos-vpp/            # VPP backend
├── danos-ovs/            # OVS-DPDK backend
├── danos-p4/             # P4Runtime / P4 backend
├── danos-models/         # YANG / OpenConfig
├── danos-mgmt/           # CLI / gNMI / NETCONF / RESTCONF
├── danos-compat/         # OcNOS-like CLI 与语义兼容层（新增）
├── danos-ha/             # NSR / VRRP / EVPN-MH / supervisor（新增）
├── danos-observability/  # telemetry / prometheus / tracing / audit（新增）
├── danos-security/       # AAA / TLS / key management / CoPP（新增）
├── danos-platform/       # x86 / ARM / generic
├── danos-test/           # topology / integration / scale / traffic / perf
├── danos-build/          # Debian / Ubuntu / container / OCI image
└── danos-docs/           # architecture / RFC / API spec / runbook（新增）
```

### 3.2 P4 子项关系澄清

**问题**：v0.4 同时提到 P4Runtime、P4C、BMv2、P4-DPDK，但第 17 节仅 `P4 test target = BMv2`。

**建议明确**：
- **P4C**：编译器，工具链依赖，非运行时
- **BMv2**：软件 P4 target，仅用于功能测试与 CI，**不用于生产**
- **P4-DPDK**：生产 P4 target，基于 DPDK 的 P4 软件 dataplane
- **P4Runtime**：控制面协议，DANOS DPA → P4Runtime Adapter → target
- 优先级：P4Runtime + BMv2（v0.4 功能验证）→ P4-DPDK（v0.5+ 生产）

### 3.3 MVP 与实施计划时间线对齐

**建议**：在第 18 节实施计划中明确每步对应的 MVP 版本：

| 实施步骤 | 对应 MVP | 验收标准 |
|---------|---------|---------|
| 1. DPA Object Model 定义 | v0.1 前置 | 所有对象 YAML/Protobuf schema 评审通过 |
| 2. DPA API Spec v0.1 | v0.1 前置 | C ABI + Protobuf + 错误码 + 版本号 |
| 3. FRR Zebra/FIB Adapter | v0.1 | ZAPI 事件 → DPA 对象映射表完成 |
| 4. VPP Backend 基础对象 | v0.1 | Route/NH/VRF/IF 通过单元测试 |
| 5. x86/ARM + VPP + DPDK CI | v0.1 | CI 绿灯，镜像可构建 |
| 6. BGP/OSPF/IS-IS/BFD E2E | v0.1 DoD | 三节点拓扑全协议通过 |
| 7. EVPN/VXLAN | v0.2 DoD | Leaf-Spine EVPN 全场景通过 |
| 8. OVS-DPDK backend | v0.3 DoD | 同一 DPA 对象双 backend 语义一致 |
| 9. P4Runtime + BMv2 | v0.4 DoD | P4 程序加载、表项下发通过 |

### 3.4 Capability 与 DPA Object 关系形式化

**建议明确**：
- Capability 是 DPA Object 的**元数据**（metadata），不是独立对象
- 每个 DPA Object 类型对应一组 Capability 声明
- 形式化：`Capability(ObjectType) → {supported: bool, max_count: uint32, features: [string], constraints: {...}}`
- Backend 启动时上报 Capability Profile，DPA 验证 Desired State 是否在 Capability 范围内

---

## 四、可实施性增强建议

### 4.1 DPA API 规范补全

**建议在第 7 节后新增"DPA API Specification"章节**，包含：

1. **API 形式**：三套等价接口
   - C ABI：`danos_dpa.h`（高性能进程内调用）
   - Protobuf + gRPC：跨进程/跨语言
   - YANG + NETCONF/gNMI：管理面

2. **错误码规范**：
   ```c
   typedef enum {
       DANOS_OK = 0,
       DANOS_ERR_INVALID_ARG = 1,
       DANOS_ERR_NOT_FOUND = 2,
       DANOS_ERR_EXISTS = 3,
       DANOS_ERR_NO_CAPACITY = 4,    // Capability 不足
       DANOS_ERR_NOT_SUPPORTED = 5,  // Backend 不支持
       DANOS_ERR_TX_CONFLICT = 6,    // 事务冲突
       DANOS_ERR_TX_TIMEOUT = 7,
       DANOS_ERR_BACKEND_DOWN = 8,
       DANOS_ERR_VERIFY_FAIL = 9,    // Programmed != Oper
       DANOS_ERR_PARTIAL = 10,       // 部分成功
   } danos_status_t;
   ```

3. **版本管理**：语义化版本 + Capability 协商
   - `dpa_get_version() → {major, minor, patch, capabilities[]}`
   - Backend 与 Core 版本兼容矩阵

4. **完整对象 API 草案**（每个对象 CRUD + 事务）：
   ```c
   danos_status_t dpa_route_create(danos_tx_t tx, const danos_route_t *route);
   danos_status_t dpa_route_read(danos_tx_t tx, uint32_t vrf, ip_prefix_t prefix, danos_route_t *out);
   danos_status_t dpa_route_update(danos_tx_t tx, const danos_route_t *route);
   danos_status_t dpa_route_delete(danos_tx_t tx, uint32_t vrf, ip_prefix_t prefix);
   danos_status_t dpa_route_dump(danos_tx_t tx, uint32_t vrf, danos_route_cb_t cb);
   ```

### 4.2 FRR ZAPI → DPA 映射表

**建议新增附录 A**，给出关键 ZAPI 消息到 DPA 对象的映射：

| ZAPI 消息 | DPA 对象 | DPA 操作 | 备注 |
|-----------|---------|---------|------|
| ZEBRA_ROUTE_ADD | Route + NHGroup | create/update | 协议字段映射 protocol |
| ZEBRA_ROUTE_DELETE | Route | delete | |
| ZEBRA_NEXTHOP_LOOKUP | NH / NHGroup | read | |
| ZEBRA_INTERFACE_ADD | Interface | create | 映射 ifindex、MTU、MAC |
| ZEBRA_INTERFACE_SET_MTU | Interface | update | |
| ZEBRA_LABELS_ADD | MPLS LSP | create | |
| ZEBRA_REDISTRIBUTE_DEFAULT | Route | update | |
| ZEBRA_BFD_DEST_REGISTER | BFD Session | create | |

### 4.3 VPP Backend 实现路径明确

**建议明确**：
- **控制通道**：VPP binary API（vat2/stat）over shared memory，**不**用 memif（memif 是数据面）
- **对象映射**：
  - DPA Interface → VPP sw_interface_index
  - DPA Route → VPP ip_route_add/del（FIB index = VRF）
  - DPA NH → VPP ip_neighbor + path
  - DPA NHGroup → VPP multipath path list
  - DPA ACL → VPP classify table + session
  - DPA QoS → VPP QoS records + policer
- **性能路径**：DPDK poll-mode driver、巨型帧、NUMA 亲和
- **故障注入**：VPP API 断连重连、session 管理

### 4.4 事务模型增强

**建议补充**：

| 维度 | 建议策略 |
|------|---------|
| 并发控制 | 多读单写：读事务无锁，写事务全局有序（单 writer 线程） |
| 隔离级别 | Read Committed + 写事务原子可见 |
| 超时 | PREPARE 阶段 100ms，COMMIT 阶段 500ms，VERIFY 阶段 1s（可配置） |
| 嵌套 | 不支持嵌套事务；子操作扁平化为单事务的多个 op |
| 持久化 | Transaction Log（WAL）持久化，用于 HA 同步与审计 |
| 回滚 | 程序化回滚（记录 inverse op）+ 状态重编程（fallback） |

### 4.5 Reconciliation 算法增强

**建议明确**：
- **触发**：事件驱动（Oper 变更通知）+ 周期对账（默认 30s，可配）
- **算法**：
  1. 计算 `diff = Desired - Programmed`
  2. 对 diff 中每个对象，按依赖拓扑序重编程
  3. 失败对象标记 `degraded`，指数退避重试（1s, 2s, 4s, ... 上限 60s）
  4. 连续 N 次失败触发告警
- **收敛性**：单调递增版本号 + 最终一致（假设 Backend 最终响应）
- **防震荡**：同一对象 5s 内最多重编程 3 次，超过则冻结并告警

### 4.6 依赖版本兼容矩阵

**建议新增附录 B**：

| DANOS-Open 版本 | FRR | VPP | DPDK | OVS | P4Runtime | Linux |
|----------------|-----|-----|------|-----|-----------|-------|
| v0.1 | 10.x | 24.x | 23.x | - | - | 6.6+ |
| v0.2 | 10.x | 24.x | 23.x | - | - | 6.6+ |
| v0.3 | 10.x | 24.x | 23.x | 3.x | - | 6.6+ |
| v0.4 | 10.x | 24.x | 23.x | 3.x | 1.x | 6.6+ |

---

## 五、技术深度增强建议

### 5.1 DPA vs SAI 对比示例

**建议新增附录 C**，给出具体 API 对比：

| 场景 | SAI API | DPA API | 差异 |
|------|---------|---------|------|
| 创建路由 | `sai_route_entry_t + sai_create_next_hop_group` | `dpa_route_create(tx, route)` | DPA 事务化、对象化；SAI 需手动管理 NH group 生命周期 |
| Capability 查询 | 无统一标准，厂商扩展 | `dpa_capability_get(obj_type)` | DPA 一等公民；SAI 依赖 `sai_query_attribute_capability` 且语义不一 |
| 事务 | 无，逐 API 调用 | `dpa_tx_begin/commit/rollback` | DPA 原生事务；SAI 需上层（如 SAI Redis）实现 |
| Backend 无关 | 假设 ASIC + SAI 实现 | 支持 VPP/OVS/P4/ASIC | DPA 不假设硬件 |

### 5.2 对象模型展开

**建议为以下对象补充完整字段定义**（YANG 或 Protobuf）：

- **ACL**：match fields（5-tuple + VLAN + MPLS + tunnel）、action（permit/deny/mirror/policer/redirect/set）、TCAM 资源声明
- **QoS**：层次（port/queue/class）、调度器（DWRR/SP/WFQ）、整形器（single/dual rate）、标记器（DSCP/802.1p）、HQoS vs flat QoS 的 Capability 区分
- **Multicast**：(*,G) 与 (S,G)、IGMP/MLD snooping、PIM-SM RP/BSR、数据面 replication tree
- **MPLS/LSP**：区分 LDP-signaled / RSVP-TE / SR-MPLS；PHP vs explicit pop；label stack
- **Tunnel**：统一抽象 + 具体子类型（VXLAN/GRE/IPIP/SR-TE/IPsec）；encap/decap 属性

### 5.3 标准对齐

**建议新增附录 D**，明确每个对象/功能对齐的 RFC：

| 功能 | 对齐 RFC |
|------|---------|
| EVPN | RFC 7432, 8365, 9161 |
| EVPN-MH | RFC 7432 §8 |
| VRRP | RFC 5798 |
| BFD | RFC 5880, 5881, 5883 |
| PIM-SM | RFC 7761 |
| IGMPv3 | RFC 3376 |
| MLDv2 | RFC 3810 |
| SR-MPLS | RFC 8660 |
| SR-TE | RFC 8667 |
| NETCONF | RFC 6241 |
| gNMI | gNMI spec |
| OpenConfig | openconfig-public |

---

## 六、风险评估与缓解（新增章节）

### 6.1 技术风险

| 风险 | 等级 | 缓解策略 |
|------|------|---------|
| VPP/OVS/P4 backend 语义不一致 | 高 | ① 定义 DPA 语义测试套件（conformance test），所有 backend 必须通过；② 同一 DPA 对象在多 backend 间的一致性测试纳入 CI；③ 差异点显式 Capability 声明，不静默 |
| FRR ZAPI 变更破坏 Adapter | 中 | ① 锁定 FRR 版本；② ZAPI 消息版本探测；③ Adapter 单元测试 mock ZAPI |
| DPDK 大版本升级 ABI 破坏 | 中 | ① 跟随 DPDK LTS；② 容器化隔离 DPDK 版本；③ CI 矩阵测试多 DPDK 版本 |
| P4 程序可移植性 | 中 | ① 限定 P4 程序子集（match-action + 状态有限）；② BMv2 与 P4-DPDK 双 target 测试 |

### 6.2 性能风险

| 风险 | 等级 | 缓解策略 |
|------|------|---------|
| 软件 dataplane 吞吐远低于 ASIC | 高 | ① 明确适用场景：边缘/DC GW/白盒中低端；② 性能基线：VPP+DPDK 单核 10Gbps+，多核 100Gbps+；③ 不承诺替代核心 ASIC 高端 |
| Reconciliation 收敛慢 | 中 | ① 事件驱动优先；② 增量 diff 而非全量；③ 并行重编程无依赖对象 |
| 大规模路由（1M+）下 VPP FIB 性能 | 中 | ① VPP FIB scale 测试；② 必要时引入 FIB 压缩/分层 |

### 6.3 生态风险

| 风险 | 等级 | 缓解策略 |
|------|------|---------|
| FRR 维护节奏 / 社区分裂 | 中 | ① 紧跟 FRR 主线；② 关键补丁上游优先；③ 维护内部 fork 仅作 fallback |
| VPP 社区方向不确定 | 中 | ① 参与 VPP 社区；② 关键特性自维护并上游；③ 评估 alternative（如 Vector Packet Processor fork） |
| DPDK 与 Linux 内核兼容 | 低 | ① 跟随 LTS 内核；② VFIO+IOMMU 强制要求 |

### 6.4 竞争风险

| 竞品 | 差异定位 |
|------|---------|
| SONiC | SONiC 依赖 SAI + ASIC；DANOS-Open DPA + 软件 dataplane 优先，ASIC 可选 |
| FRR alone | FRR 无统一数据面抽象、事务、状态协调；DANOS-Open 在 FRR 之上补齐 NOS 框架 |
| Juniper cRPD / Nokia SR-Linux | 商业 cRPD 闭源；DANOS-Open 全开源 |

### 6.5 合规风险

| 风险 | 缓解 |
|------|------|
| GPLv2（FRR）传染性 | DANOS-Open 通过进程间通信（ZAPI）与 FRR 隔离，不静态/动态链接 FRR 库；Adapter 独立进程 |
| Apache-2.0（VPP）与 GPLv2 兼容 | 组合分发时遵守两者条款 |
| DPDK BSD-3-Clause | 宽松，无传染 |
| 商标 | "DANOS" 商标需确认 AT&T/LF 权属，必要时更名 |

---

## 七、路线图增强建议

### 7.1 MVP 验收标准（DoD）补全

**v0.1 DoD**：
- [ ] DPA API v0.1 规范发布（C ABI + Protobuf + 错误码）
- [ ] VPP Backend 实现 Interface/VLAN/VRF/Route/NH/NHGroup/ACL/QoS(基础)
- [ ] FRR BGP/OSPF/IS-IS/BFD → ZAPI → DPA → VPP 端到端打通
- [ ] 三节点拓扑：BGP ECMP、OSPF、IS-IS、BFD 快速收敛
- [ ] YANG 模型 + NETCONF + CLI 基础可用
- [ ] CI：x86_64 + aarch64，每 PR 触发
- [ ] 性能基线：单核 BGP converge 1K routes < 5s

**v0.2 DoD**：
- [ ] PIM-SM/IGMP/MLD（FRR pimd/pim6d）
- [ ] MPLS LDP + LSP + VPNv4/VPNv6
- [ ] VXLAN + EVPN（Type 2/3/4/5）+ IRB
- [ ] Leaf-Spine EVPN 全场景测试通过
- [ ] Multicast 基础场景

**v0.3 DoD**：
- [ ] OVS-DPDK Backend 实现与 VPP 等价对象集
- [ ] **一致性测试套件**：同一 DPA 操作在 VPP 与 OVS 产生等价转发语义
- [ ] Backend 切换：运行时切换 dataplane（维护窗口）

**v0.4 DoD**：
- [ ] P4Runtime Adapter + BMv2 target
- [ ] 基础 P4 程序（L2/L3/VXLAN）加载与表项下发
- [ ] P4 target conformance test 通过

### 7.2 长期路线（v0.5+）

| 版本 | 主题 | 关键特性 |
|------|------|---------|
| v0.5 | 生产加固 | P4-DPDK、HA（NSR/EVPN-MH）、安全（AAA/TLS/CoPP）、Telemetry |
| v0.6 | 规模化 | 1M 路由、1000 EVPN EVI、大规模 ACL、性能调优 |
| v0.7 | 生态集成 | K8s Operator、Terraform Provider、Ansible collection |
| v1.0 | GA | 全文档、多 backend 一致性认证、性能 SLA、升级路径 |

---

## 八、文档质量改进建议

### 8.1 结构性改进

1. **增加目录与交叉引用**：章节间相互引用（如"见 §7 DPA Object Model"）
2. **增加图表**：
   - 总体架构图（Mermaid/PlantUML）
   - DPA 对象关系图（UML 类图）
   - 事务状态机图
   - Reconciliation 流程图
   - FRR→DPA→Backend 时序图
3. **增加附录**：API 完整规范、映射表、版本矩阵、RFC 对齐表
4. **增加术语表**：DPA、SAI、EVI、ESI、IRB、CoPP 等定义

### 8.2 形式化改进

1. **YANG 模型**：所有 DPA 对象给出 YANG 定义
2. **Protobuf IDL**：所有 API 给出 `.proto` 定义
3. **C ABI 头文件**：完整 `danos_dpa.h`
4. **错误码枚举**：完整定义
5. **Capability Schema**：YANG 或 Protobuf 格式

### 8.3 可追溯性

1. 每条架构决策（第 17 节）标注**决策依据**与**替代方案**
2. 每个 MVP 特性标注**对应章节**与**测试用例**
3. 每个风险标注**对应缓解措施**与**责任人**

---

## 九、优先级排序的改进动作清单

### P0（必须在 v1.0 规格发布前完成）

1. 修复仓库结构不一致（§3.1）
2. 补全 DPA API 规范（C ABI + Protobuf + 错误码 + 版本）（§4.1）
3. 补全 MVP v0.1 DoD 与验收标准（§7.1）
4. 新增风险评估章节（§6）
5. 新增安全架构章节（§2.1 第 20 节）
6. 新增 HA 设计章节（§2.1 第 21 节）
7. 新增可观测性章节（§2.1 第 22 节）
8. 明确 Capability 与 DPA Object 关系（§3.4）

### P1（v0.2 前完成）

9. FRR ZAPI → DPA 映射表（§4.2）
10. VPP Backend 实现路径明确（§4.3）
11. 事务模型增强（§4.4）
12. Reconciliation 算法增强（§4.5）
13. 对象模型展开（ACL/QoS/Multicast/MPLS/Tunnel）（§5.2）
14. 标准对齐表（§5.3）
15. 依赖版本矩阵（§4.6）
16. 生命周期与升级章节（§2.1 第 23 节）

### P2（v0.4 前完成）

17. 部署与编排章节（§2.1 第 24 节）
18. 许可证与社区治理章节（§2.1 第 25 节）
19. DPA vs SAI 对比示例（§5.1）
20. 文档结构性与形式化改进（§8）

---

## 十、结论

v1.0 文档作为**架构原则与方向声明**是合格的，四条核心原则立场清晰，技术选型（FRR + VPP + DPDK + P4）合理，MVP 路线循序渐进。

但要成为可指导工程实施的**架构规格说明书**，需在以下三个方向系统性补强：

1. **可实施性**：API 规范、映射表、错误码、版本管理、验收标准必须具体化
2. **完整性**：安全、HA、可观测性、风险四大缺失章节必须补齐
3. **一致性**：仓库结构、P4 子项、MVP 与实施计划时间线必须对齐

建议在 v1.1 文档中按 P0/P1/P2 优先级推进上述改进动作清单，预计可使文档达到可指导 v0.1 实施的规格成熟度。

---

*本评审报告基于 v1.0 文档全文分析生成，所有建议均给出具体可执行内容，可直接用于 v1.1 文档修订。*
