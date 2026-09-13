# DANOS-Open 仓库结构与模块职责说明 v1.1

> 修订自 v1.0 第 13 节。修复了仓库结构与正文模块引用不一致的硬伤，
> 补齐 `danos-compat/`、`danos-ha/`、`danos-observability/`、`danos-security/`、`danos-docs/`。

## 1. 顶层仓库结构

```
danos-open/
├── danos-core/            # 核心引擎：object / state / transaction / capability / event
├── danos-dpa/             # DPA 公共 API（C ABI + Protobuf IDL + 错误码 + 版本协商）
├── danos-fib/             # FRR zebra / FIB Adapter（ZAPI → DPA 映射）
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
├── NOTICE
└── README.md
```

## 2. 模块职责详解

### 2.1 `danos-core/` — 核心引擎

**职责**：DANOS-Open 的核心 IP，不依赖任何具体 dataplane。

```
danos-core/
├── include/danos/core/    # 公共头文件
│   ├── object.h           # 对象基类与注册
│   ├── state.h            # Desired / Programmed / Oper 状态管理
│   ├── transaction.h      # 事务引擎（状态机、WAL、并发控制）
│   ├── capability.h       # Capability 注册表与校验
│   ├── event.h            # 事件总线（pub/sub）
│   └── reconciler.h       # Reconciliation 引擎
├── src/
│   ├── object/            # 各对象类型实现（route, nh, acl, ...）
│   ├── state/             # 状态存储（in-memory + persistent snapshot）
│   ├── transaction/       # 事务状态机、WAL、锁
│   ├── capability/        # Capability 聚合与校验
│   ├── event/             # 事件总线实现
│   └── reconciler/        # Reconciler 算法（事件驱动 + 周期对账 + 退避）
└── tests/                 # 单元测试
```

**关键约束**：
- 不依赖 VPP / OVS / P4 / DPDK / FRR
- 不依赖任何厂商 SDK
- 通过 `danos-dpa/` 定义的 API 与外部交互

### 2.2 `danos-dpa/` — DPA 公共 API

**职责**：定义所有 backend 必须实现的稳定 API。

```
danos-dpa/
├── include/danos/dpa.h    # C ABI 头文件（v0.1 完整定义）
├── proto/danos_dpa.proto  # Protobuf IDL + gRPC service
├── spec/danos_dpa_spec.md # API 语义规范（错误码、版本、事务、并发）
├── errors/                # 错误码定义与字符串映射
├── version/               # 版本协商实现
└── tests/                 # API conformance 测试套件
```

**关键约束**：
- API 稳定性承诺：同 major 内向后兼容
- 三套等价接口（C ABI / Protobuf / YANG）语义一致
- conformance 测试套件所有 backend 必须通过

### 2.3 `danos-fib/` — FRR FIB Adapter

**职责**：将 FRR zebra 的 ZAPI 消息归一化为 DPA 对象操作。

```
danos-fib/
├── src/
│   ├── zapi/              # ZAPI 消息解析（route, nh, interface, labels, bfd）
│   ├── mapper/            # ZAPI → DPA 对象映射表
│   ├── adapter/           # 主适配逻辑（事件驱动 → 事务）
│   └── session/           # FRR zebra 连接管理（重连、健康检查）
├── config/                # FRR 集成配置
└── tests/                 # 映射测试（mock ZAPI）
```

**关键约束**：
- 独立进程，通过 gRPC 与 DPA Core 通信
- 不直接调用 VPP / OVS / P4
- 不静态/动态链接 FRR 库（避免 GPLv2 传染），通过 ZAPI socket 通信

### 2.4 `danos-vpp/` — VPP Backend

**职责**：将 DPA API 映射到 VPP binary API。

```
danos-vpp/
├── src/
│   ├── api/               # VPP binary API 客户端（shm 接口）
│   ├── mapper/            # DPA 对象 → VPP API 映射
│   │   ├── iface.c        # DPA Interface → VPP sw_interface
│   │   ├── route.c        # DPA Route → VPP ip_route_add/del
│   │   ├── nh.c           # DPA NH/NHGroup → VIP path / multipath
│   │   ├── vrf.c          # DPA VRF → VPP FIB table
│   │   ├── acl.c          # DPA ACL → VPP classify table/session
│   │   ├── qos.c          # DPA QoS → VPP QoS records/policer
│   │   ├── mpls.c         # DPA MPLS → VPP mpls tunnel/path
│   │   ├── tunnel.c       # DPA Tunnel → VPP vxlan/gre/ipip
│   │   └── evpn.c         # DPA EVPN → VPP vxlan + bd + fib
│   ├── capability/        # VPP Capability 上报
│   └── session/           # VPP API 连接管理（stat segment、binary API）
├── plugin/                # VPP 插件（如需自定义 graph node）
└── tests/                 # 单元测试 + VPP 联调测试
```

**关键约束**：
- 通过 VPP binary API（shared memory）通信，不用 memif（memif 是数据面）
- Capability 上报真实反映 VPP 能力（不夸大）
- 性能路径：DPDK poll-mode driver、NUMA 亲和

### 2.5 `danos-ovs/` — OVS-DPDK Backend

**职责**：将 DPA API 映射到 OVS OpenFlow + OVSDB。

```
danos-ovs/
├── src/
│   ├── ovsdb/             # OVSDB 客户端
│   ├── ofctrl/            # OpenFlow 控制通道
│   ├── mapper/            # DPA 对象 → OVS 流表 / OVSDB
│   ├── capability/        # OVS Capability 上报
│   └── session/           # OVS 连接管理
└── tests/
```

**关键约束**：
- v0.3 引入，用于验证 DPA 抽象的多 backend 一致性
- 不作为生产主路径（VPP 是 primary）

### 2.6 `danos-p4/` — P4 Backend

**职责**：将 DPA API 映射到 P4Runtime。

```
danos-p4/
├── src/
│   ├── p4rt/              # P4Runtime 客户端
│   ├── mapper/            # DPA 对象 → P4 表项
│   ├── capability/        # P4 target Capability 上报
│   └── session/           # P4Runtime 连接管理
├── p4src/                 # P4 程序源码（L2/L3/VXLAN 基础）
├── targets/
│   ├── bmv2/              # BMv2 target（仅测试）
│   └── dpdk/              # P4-DPDK target（生产）
└── tests/
```

**关键约束**：
- BMv2 仅用于功能测试与 CI，不用于生产
- P4-DPDK 是生产 P4 target（v0.5+）
- P4C 是编译器工具链依赖，非运行时

### 2.7 `danos-models/` — YANG / OpenConfig

**职责**：所有配置与状态数据的 YANG 模型。

```
danos-models/
├── yang/
│   ├── danos-types/       # 公共类型定义
│   ├── danos-iface/       # Interface 模型
│   ├── danos-route/       # Route / NH / NHGroup 模型
│   ├── danos-acl/         # ACL 模型
│   ├── danos-qos/         # QoS 模型
│   ├── danos-mpls/        # MPLS 模型
│   ├── danos-evpn/        # EVPN 模型
│   ├── danos-bfd/         # BFD 模型
│   └── danos-system/      # 系统管理模型
├── openconfig/            # OpenConfig 模型（子集或扩展）
└── generated/             # 从 YANG 生成的 C/Go/Python 绑定
```

### 2.8 `danos-mgmt/` — 管理面

**职责**：北向接口。

```
danos-mgmt/
├── cli/                   # 交互式 CLI（基于 Click/Python）
├── gnmi/                  # gNMI server（Get/Set/Subscribe）
├── netconf/               # NETCONF server
├── restconf/              # RESTCONF server
└── common/                # 共享：认证、会话、事务包装
```

### 2.9 `danos-compat/` — OcNOS 兼容层

**职责**：OcNOS-like CLI 与旧配置语义映射。

```
danos-compat/
├── cli/                   # OcNOS 风格 CLI 命令
├── config/                # OcNOS 配置格式解析与转换
├── semantic/              # 语义映射（OcNOS 行为 → DPA 对象）
└── tests/                 # 兼容性测试
```

**关键约束**：
- 路径：Compatibility Layer → DANOS Model → DPA，**不**直接操作 VPP
- 兼容 ≠ 优先实现：EVPN/SR 优先原则不变，兼容层仅提供 legacy 接口

### 2.10 `danos-ha/` — 高可用

**职责**：进程级与节点级 HA。

```
danos-ha/
├── supervisor/            # 进程监管（重启策略、热补丁）
├── nsr/                   # Non-Stop Routing（FRR nsr 集成）
├── vrrp/                  # VRRP / M-VRRP
├── evpn-mh/               # EVPN Multihoming（ESI、Alias、Per-EVI AD）
├── issu/                  # In-Service Software Upgrade
├── state-sync/            # 路由引擎间状态同步
└── tests/
```

### 2.11 `danos-observability/` — 可观测性

**职责**：Telemetry、监控、追踪、审计。

```
danos-observability/
├── telemetry/             # gNMI Subscribe 流式 telemetry
├── prometheus/            # Prometheus exporter + Grafana dashboard
├── tracing/               # OpenTelemetry 分布式追踪
├── audit/                 # 审计日志（Transaction ID 链）
├── logging/               # 结构化日志（JSON）+ 集中收集
├── alerting/              # Alertmanager 规则 + Runbook
└── dashboards/            # 预置 Grafana dashboard JSON
```

### 2.12 `danos-security/` — 安全

**职责**：认证、授权、加密、CoPP。

```
danos-security/
├── aaa/                   # RBAC + TACACS+ / RADIUS
├── tls/                   # TLS / mTLS / 证书管理
├── keys/                  # 密钥管理（KMS 集成、轮换）
├── copp/                  # Control Plane Policing
├── ssh/                   # SSH server（管理面）
└── tests/
```

### 2.13 `danos-platform/` — 平台适配

```
danos-platform/
├── x86_64/                # x86 平台（ONL 兼容、DPDK 绑定）
├── aarch64/               # ARM 平台
└── generic/               # 通用容器/VM 平台
```

### 2.14 `danos-test/` — 测试

```
danos-test/
├── unit/                  # 单元测试
├── integration/           # 集成测试（多模块联调）
├── topology/              # 拓扑测试（Containerlab）
├── scale/                 # 规模测试（1K/10K/100K/1M 路由）
├── traffic/               # 流量测试（TRex / scapy）
├── perf/                  # 性能基线测试
├── conformance/           # DPA API conformance（所有 backend 必过）
├── consistency/           # 多 backend 语义一致性
└── ci/                    # CI 流水线定义
```

### 2.15 `danos-build/` — 构建与打包

```
danos-build/
├── debian/                # Debian 包构建
├── ubuntu/                # Ubuntu 包构建
├── container/             # Dockerfile
├── oci/                   # OCI 镜像 + 签名（cosign）+ SBOM
└── deps/                  # 依赖版本锁定（FRR/VPP/DPDK 兼容矩阵）
```

### 2.16 `danos-docs/` — 文档

```
danos-docs/
├── architecture/          # 架构文档（本文档系列）
├── api/                   # API 规范（DPA、gNMI、NETCONF）
├── rfc/                   # 内部 RFC（设计提案）
├── adr/                   # Architecture Decision Records
├── runbook/               # 运维手册
└── tutorials/             # 快速入门
```

## 3. 模块依赖关系

```
                    danos-mgmt
                        │
                        ▼
                   danos-models
                        │
                        ▼
                   danos-core ◄──── danos-dpa (API 定义)
                        │
          ┌─────────────┼─────────────┐
          ▼             ▼             ▼
      danos-vpp     danos-ovs     danos-p4
                        │
                        ▼
                  danos-platform

  danos-fib ──► (gRPC) ──► danos-core
  danos-compat ──► danos-models ──► danos-core
  danos-ha ──► danos-core
  danos-observability ──► (events) danos-core
  danos-security ──► danos-mgmt + danos-core
```

**关键依赖规则**：
1. `danos-core` 不依赖任何 backend
2. backend 不依赖彼此
3. `danos-fib` 通过 gRPC 与 core 通信，不直接链接 FRR
4. `danos-compat` 不直接操作 backend，必须经 core
5. 所有模块依赖 `danos-dpa` 的 API 定义

## 4. 许可证策略

| 模块 | 许可证 | 理由 |
|------|--------|------|
| danos-core / dpa / vpp / ovs / p4 / models / mgmt / ha / observability / security / platform / test / build / docs | Apache-2.0 | 主代码，宽松许可 |
| danos-fib | Apache-2.0 | 通过 ZAPI socket 与 FRR 通信，不链接 FRR 库，无 GPLv2 传染 |
| danos-compat | Apache-2.0 | 兼容层独立实现 |

**GPLv2 隔离策略**：
- FRR 以独立进程运行，通过 ZAPI socket 通信
- DANOS-Open 不静态/动态链接 FRR 库
- 组合分发时遵守 GPLv2 + Apache-2.0 双条款
