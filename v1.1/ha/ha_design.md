# 第 21 章 高可用 (HA) 设计

> v1.1 新增章节。HA 不能后补，BFD/EVPN-MH 应进入 v0.1 或最迟 v0.2。

## 21.1 HA 层次

| 层次 | 机制 | 收敛目标 | 章节 |
|------|------|---------|------|
| 进程级 | supervisor + 重启策略 | < 1s | §21.2 |
| 协议级 NSR | FRR nsr / Graceful Restart | < 0s（无感知） | §21.3 |
| 链路级 | BFD + LACP | < 1s 检测 | §21.4 |
| 网关级 | VRRP / EVPN-MH | < 1s 切换 | §21.5 |
| 节点级 | 双路由引擎 + 状态同步 | < 30s | §21.6 |
| 软件级 | ISSU / 滚动升级 | 0s（无感知） | §21.7 |

## 21.2 进程级 HA

### 21.2.1 Supervisor

- 每个关键进程（dpa-core、frr-zebra、vpp、mgmt）由 supervisor 监管
- 重启策略：
  | 策略 | 说明 |
  |------|------|
  | always | 崩溃后立即重启 |
  | on-failure | 非零退出码重启 |
  | never | 不重启（需人工介入） |
- 重启退避：1s, 2s, 4s, 8s, 上限 60s
- 连续 5 次启动失败 → 告警 + 进入 safe mode

### 21.2.2 热补丁

- 支持函数级热补丁（livepatch）
- 补丁签名验证后加载
- 可回滚

## 21.3 Non-Stop Routing (NSR)

### 21.3.1 目标

路由引擎切换时，协议邻居无感知，路由不中断。

### 21.3.2 实现

- 集成 FRR `nsr` 机制
- 主备路由引擎间同步：协议状态机、LSA/LSP/RIB
- 切换时备引擎已具备完整状态，无需重新收敛
- 同步通道：专用 HA 链路或带内

### 21.3.3 Graceful Restart (GR) 降级

- NSR 不可用时降级为 GR
- GR 通知邻居保持路由（RFC 4724 BGP, RFC 3623 OSPF）
- GR 期间数据面保持最后已知状态

## 21.4 链路级 HA

### 21.4.1 BFD

| 参数 | 单跳默认 | 多跳默认 |
|------|---------|---------|
| desired_tx | 300ms | 1s |
| required_rx | 300ms | 1s |
| detect_mult | 3 | 3 |
| 检测时间 | ~900ms | ~3s |

- BFD 与 BGP/OSPF/IS-IS 联动：BFD down → 协议 session down
- 微秒级 BFD（可选）：desired_tx=10ms，需硬件辅助

### 21.4.2 LACP

- LACP partner 故障检测：3x period（默认 30s → 90s，fast 模式 1s → 3s）
- LACP 与 BFD 联动：member BFD down → 从 bond 中移除

## 21.5 网关级 HA

### 21.5.1 VRRP

- VRRPv3（IPv4/IPv6）
- 优先级 + 抢占
- VRRP 与 BFD 联动：Master BFD down → Backup 立即接管（< 1s）
- M-VRRP：多组 VRRP，负载分担

### 21.5.2 EVPN Multihoming (EVPN-MH)

- 对齐 RFC 7432 §8
- ESI（Ethernet Segment Identifier）标识同一 ES 的多链路
- Type-1 AD：Mass-withdrawal，快速收敛
- Type-2 AD：Alias，无需所有 PE 发 Type-2
- Type-4 AD：Per-EVI / Per-ES
- 模式：All-Active（AA）与 Single-Active（SA）
- 故障切换：远端 PE 检测 ESI down → 删除关联 MAC → 流量切换 < 1s

### 21.5.3 EVPN-MH vs ICCP

- EVPN-MH 是首选方案（v1.0 原则：EVPN-MH > ICCP）
- ICCP 仅在必须兼容 MLAG 旧设备时考虑，P3 优先级

## 21.6 节点级 HA（双路由引擎）

### 21.6.1 架构

```
┌─── 路由引擎 A (Active) ───┐    ┌─── 路由引擎 B (Standby) ───┐
│  FRR (BGP/OSPF/...)      │    │  FRR (BGP/OSPF/...)        │
│  DPA Core                │◄──►│  DPA Core                  │
│  VPP (dataplane)         │ HA │  VPP (dataplane, standby)  │
│  Mgmt                    │sync│  Mgmt                      │
└──────────────────────────┘    └────────────────────────────┘
                  │                            │
                  └──────── 线卡/转发面 ───────┘
```

### 21.6.2 状态同步

- 同步内容：DPA Desired State、FRR 协议状态、Transaction WAL
- 同步通道：专用 HA 端口（推荐 10G+）
- 同步模式：热备（实时同步，切换无感知）

### 21.6.3 切换流程

1. Active 故障检测（heartbeat 超时 / BFD down）
2. Standby 提升为 Active（< 1s）
3. 数据面切换（线卡重连到新 Active）
4. 管理面切换（VIP/管理 IP 漂移）
5. 总切换时间目标：< 30s

## 21.7 In-Service Software Upgrade (ISSU)

### 21.7.1 滚动升级流程

1. 升级 Standby 路由引擎
2. 验证 Standby 健康
3. 切换 Active → 新升级的 Standby
4. 升级原 Active（现 Standby）
5. 验证双引擎健康

### 21.7.2 兼容性

- DPA API 版本协商：新旧版本可共存于 major 内
- YANG 模型版本：新增节点不破坏旧客户端
- 数据面兼容：VPP 升级期间保持转发

### 21.7.3 回滚

- 升级失败自动回滚到前一版本
- 回滚基于 Transaction WAL + 镜像快照

## 21.8 HA 章节 MVP 对齐

| 特性 | v0.1 | v0.2 | v0.5+ |
|------|------|------|-------|
| 进程 supervisor | ✅ | | |
| BFD 单跳 | ✅ | | |
| BFD 多跳 | | ✅ | |
| LACP | ✅ | | |
| VRRP | | ✅ | |
| EVPN-MH | | ✅ | |
| NSR | | | ✅ |
| 双路由引擎 | | | ✅ |
| ISSU | | | ✅ |
| 热补丁 | | | ✅ |
