# 第 26 章 风险评估与缓解

> v1.1 新增章节。v1.0 完全缺失风险评估，本章补齐。

## 26.1 技术风险

### 26.1.1 多 Backend 语义不一致

| 维度 | 内容 |
|------|------|
| 风险 | VPP / OVS / P4 backend 对同一 DPA 对象的转发语义不一致 |
| 等级 | 高 |
| 影响 | 配置在 backend A 正确但在 backend B 行为异常；切换 backend 导致故障 |
| 缓解 | ① DPA API conformance 测试套件，所有 backend 必须通过（`danos-test/conformance/`）<br>② 多 backend 一致性测试：同一 DPA 操作在 VPP 与 OVS 产生等价转发语义（`danos-test/consistency/`）<br>③ 差异点显式 Capability 声明，不静默（如 OVS 不支持 IRB 则 Capability 声明）<br>④ 每个对象定义"语义不变量"文档，backend 实现必须满足 |
| 残余风险 | 极端场景（如分片、异常包）可能仍有差异 → 模糊测试持续发现 |

### 26.1.2 FRR ZAPI 变更破坏 Adapter

| 维度 | 内容 |
|------|------|
| 风险 | FRR 升级后 ZAPI 消息格式变更，Adapter 解析失败 |
| 等级 | 中 |
| 缓解 | ① 锁定 FRR 版本（兼容矩阵）<br>② ZAPI 消息版本探测（FRR 支持 version negotiation）<br>③ Adapter 单元测试 mock ZAPI 消息<br>④ CI 矩阵测试多 FRR 版本 |
| 残余风险 | FRR 大版本（如 ZAPI 2.0）可能需要 Adapter 重写 |

### 26.1.3 DPDK 大版本 ABI 破坏

| 维度 | 内容 |
|------|------|
| 风险 | DPDK 升级（如 23.x → 24.x）ABI 不兼容，VPP 重新编译失败 |
| 等级 | 中 |
| 缓解 | ① 跟随 DPDK LTS 版本<br>② 容器化隔离 DPDK 版本（每容器绑定特定 DPDK）<br>③ CI 矩阵测试多 DPDK 版本<br>④ VPP 与 DPDK 版本绑定发布 |
| 残余风险 | DPDK LTS 停止维护 → 提前评估迁移 |

### 26.1.4 P4 程序可移植性

| 维度 | 内容 |
|------|------|
| 风险 | P4 程序在 BMv2 通过但在 P4-DPDK 失败 |
| 等级 | 中 |
| 缓解 | ① 限定 P4 程序子集（match-action + 有限状态）<br>② BMv2 与 P4-DPDK 双 target 测试<br>③ P4C 编译器版本锁定 |
| 残余风险 | 复杂 P4 程序可能无法在 P4-DPDK 运行 → 限定 v0.4 范围 |

## 26.2 性能风险

### 26.2.1 软件 dataplane 吞吐远低于 ASIC

| 维度 | 内容 |
|------|------|
| 风险 | 纯软件 dataplane 吞吐与 ASIC 相差 1-2 个数量级 |
| 等级 | 高 |
| 影响 | 无法替代核心 ASIC 高端场景 |
| 缓解 | ① 明确适用场景：边缘 / DC GW / 白盒中低端 / vCPE<br>② 性能基线：VPP+DPDK 单核 10Gbps+，多核 100Gbps+（64B 包）<br>③ 不承诺替代核心 ASIC 高端<br>④ 文档明确性能边界与适用场景<br>⑤ 长期：可选 ASIC backend（通过 DPA 适配，但不作为核心依赖） |
| 残余风险 | 用户期望过高 → 文档与售前明确管理期望 |

### 26.2.2 Reconciliation 收敛慢

| 维度 | 内容 |
|------|------|
| 风险 | 大规模（1M 路由）下 reconcile 周期对账耗时长 |
| 等级 | 中 |
| 缓解 | ① 事件驱动优先（oper 变更立即触发）<br>② 增量 diff 而非全量<br>③ 并行重编程无依赖对象<br>④ 周期对账可配置（大规模场景调大周期） |
| 拼余风险 | 极端场景下事件丢失 → 周期对账兜底 |

### 26.2.3 大规模路由下 VPP FIB 性能

| 维度 | 内容 |
|------|------|
| 风险 | 1M+ 路由下 VPP FIB 查找性能下降 |
| 等级 | 中 |
| 缓解 | ① VPP FIB scale 测试（1M 路由基线）<br>② 必要时引入 FIB 压缩 / 分层<br>③ VPP 性能调优（NUMA、hugepage、CPU 亲和） |
| 拼余风险 | 超大规模（10M+）可能需要专用优化 |

## 26.3 生态风险

### 26.3.1 FRR 维护节奏 / 社区分裂

| 维度 | 内容 |
|------|------|
| 风险 | FRR 社区分裂或维护放缓，关键 bug 无人修复 |
| 等级 | 中 |
| 缓解 | ① 紧跟 FRR 主线<br>② 关键补丁上游优先<br>③ 维护内部 fork 仅作 fallback<br>④ 参与 FRR 社区治理 |
| 拼余风险 | FRR 项目停止 → 评估替代（如 GoBGP + 自研协议栈，但成本高） |

### 26.3.2 VPP 社区方向不确定

| 维度 | 内容 |
|------|------|
| 风险 | VPP 社区方向变化（如重点转向非网络用例） |
| 等级 | 中 |
| 缓解 | ① 参与 VPP 社区<br>② 关键特性自维护并上游<br>③ 评估 alternative（如 Vector Packet Processor fork） |
| 拼余风险 | VPP 大版本重构 → DPA Adapter 适配成本 |

### 26.3.3 DPDK 与 Linux 内核兼容

| 维度 | 内容 |
|------|------|
| 风险 | DPDK 与新内核版本不兼容 |
| 等级 | 低 |
| 缓解 | ① 跟随 LTS 内核<br>② VFIO+IOMMU 强制要求<br>③ 容器化隔离内核版本 |

## 26.4 竞争风险

| 竞品 | 差异定位 | 风险 | 缓解 |
|------|---------|------|------|
| SONiC | SONiC 依赖 SAI + ASIC；DANOS-Open DPA + 软件 dataplane 优先 | 用户选择 SONiC 因生态成熟 | ① 明确差异：DANOS-Open 不依赖 ASIC SDK<br>② 兼容 SONiC 部分模型（YANG/OpenConfig）<br>③ 提供 SONiC 迁移指南 |
| FRR alone | FRR 无统一数据面抽象、事务、状态协调 | 用户直接用 FRR + 脚本 | ① 明确差异：DANOS-Open 在 FRR 之上补齐 NOS 框架<br>② 提供 FRR-only → DANOS-Open 迁移路径 |
| Juniper cRPD | 商业 cRPD 闭源但成熟 | 用户选商业方案 | ① 全开源优势<br>② 无厂商锁定<br>③ 社区生态 |
| Nokia SR-Linux | 商业 NOS，成熟 | 同上 | 同上 |

## 26.5 合规风险

### 26.5.1 GPLv2 传染性

| 维度 | 内容 |
|------|------|
| 风险 | FRR GPLv2，若 DANOS-Open 链接 FRR 库则传染 |
| 等级 | 中 |
| 缓解 | ① FRR 以独立进程运行，通过 ZAPI socket 通信<br>② DANOS-Open 不静态/动态链接 FRR 库<br>③ 组合分发时遵守 GPLv2 + Apache-2.0 双条款<br>④ 法务审查确认隔离方案 |
| 拼余风险 | 法律解释差异 → 法务最终确认 |

### 26.5.2 商标

| 维度 | 内容 |
|------|------|
| 风险 | "DANOS" 商标权属未确认 |
| 等级 | 中 |
| 缓解 | ① 确认 AT&T / LF Networking 权属<br>② 必要时更名（如 "OpenDPA-NOS"）<br>③ 项目早期即明确商标策略 |

### 26.5.3 开源协议组合

| 组合 | 兼容性 | 风险 |
|------|--------|------|
| Apache-2.0 (DANOS) + GPLv2 (FRR) | 兼容（进程隔离） | 低 |
| Apache-2.0 (DANOS) + Apache-2.0 (VPP) | 兼容 | 无 |
| Apache-2.0 (DANOS) + BSD-3 (DPDK) | 兼容 | 无 |
| Apache-2.0 (DANOS) + Apache-2.0 (OVS) | 兼容 | 无 |

## 26.6 风险登记册

| ID | 风险 | 类别 | 等级 | 状态 | 责任人 | 缓解措施 |
|----|------|------|------|------|--------|---------|
| R-T-01 | 多 backend 语义不一致 | 技术 | 高 | 开放 | core-team | conformance + consistency 测试 |
| R-T-02 | FRR ZAPI 变更 | 技术 | 中 | 开放 | fib-team | 版本锁定 + mock 测试 |
| R-T-03 | DPDK ABI 破坏 | 技术 | 中 | 开放 | vpp-team | LTS + 容器隔离 |
| R-T-04 | P4 可移植性 | 技术 | 中 | 开放 | p4-team | 程序子集 + 双 target |
| R-P-01 | 软件 dataplane 性能 | 性能 | 高 | 开放 | arch | 场景限定 + 基线 |
| R-P-02 | Reconcile 收敛慢 | 性能 | 中 | 开放 | core-team | 事件驱动 + 增量 |
| R-P-03 | 大规模 FIB 性能 | 性能 | 中 | 开放 | vpp-team | scale 测试 + 调优 |
| R-E-01 | FRR 社区 | 生态 | 中 | 开放 | arch | 跟主线 + 内部 fork |
| R-E-02 | VPP 社区 | 生态 | 中 | 开放 | arch | 参与社区 + 自维护 |
| R-C-01 | SONiC 竞争 | 竞争 | 中 | 开放 | arch | 差异定位 + 迁移指南 |
| R-L-01 | GPLv2 传染 | 合规 | 中 | 开放 | legal | 进程隔离 + 法务确认 |
| R-L-02 | 商标权属 | 合规 | 中 | 开放 | legal | 确认或更名 |

## 26.7 风险评审机制

- 每月风险评审会议
- 新风险及时登记
- 已缓解风险标记关闭
- 高等级风险升级到架构评审
