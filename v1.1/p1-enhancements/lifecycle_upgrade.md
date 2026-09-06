# 第 23 章 生命周期与升级管理（v1.1 P1-16）

> v1.1 评审 §2.1 第 23 章 P1 改进动作。滚动升级、A/B 部署、配置回滚、
> 镜像管理、兼容性。对应 `danos-ha/` 模块。

## 23.1 滚动升级（Rolling Upgrade）

### 23.1.1 节点级升级流程

```
1. cordon: 标记节点为"维护中"，停止接受新 BGP/OSPF 邻居
2. drain: 等待控制面收敛（BGP GR/NSR 维持转发），停止管理面写入
3. upgrade: 替换 DANOS-Open 二进制/容器镜像
4. restart: 重启进程，WAL 重放恢复事务状态
5. verify: Reconciler 全量对账，确认 Programmed = Desired
6. uncordon: 恢复接受邻居，恢复管理面
```

### 23.1.2 控制面无中断保证

| 机制 | 作用 |
|------|------|
| BGP Graceful Restart (RFC 4724) | 升级期间邻居保持会话，转发不中断 |
| NSR (FRR nsr) | 路由引擎主备同步，主重启时备接管 |
| WAL 重放 | 升级后恢复未完成事务 |
| Reconciler | 升级后全量对账，修复漂移 |

### 23.1.3 数据面无中断保证

| 机制 | 作用 |
|------|------|
| VPP hitless upgrade | VPP 支持热升级（v0.3+） |
| 双 dataplane A/B | 见 §23.2 |

## 23.2 A/B 部署

### 23.2.1 双 dataplane 并行

```
┌─────────────┐
│  Management  │
└──────┬──────┘
       │
┌──────┴──────┐
│   DPA Core   │
└──┬──────┬──┘
   │A     │B
┌──┴──┐ ┌─┴──┐
│ VPP │ │VPP'│  (新版本)
│旧版 │ │新版 │
└─────┘ └────┘
```

### 23.2.2 流量切换流程

1. 部署 dataplane B（新版本），不接流量
2. 将 Desired 状态同步到 B（Reconciler）
3. 验证 B 的 Programmed = Desired
4. 原子切换流量从 A 到 B（VPP bond/VRRP）
5. 观察 B 健康状态，异常则回切 A
6. 确认稳定后下线 A

### 23.2.3 适用场景

| 场景 | 适用 |
|------|------|
| VPP major 升级 | ✓（dataplane ABI 可能 break） |
| DPDK 升级 | ✓ |
| 紧急回滚 | ✓（切回 A） |
| 配置变更验证 | ✓ |

## 23.3 配置回滚

### 23.3.1 Transaction ID 链式回滚

每个 Transaction 有唯一 ID（`danos_tx_id_t`），WAL 记录所有变更。
回滚按 Transaction ID 逆序执行 inverse op。

```
当前状态: T5(T4(T3(T2(T1(initial))))
回滚到 T3 后: T3(T2(T1(initial)))
执行: inverse(T5), inverse(T4)
```

### 23.3.2 回滚 API

```c
// 回滚到指定 Transaction 之前的状态
danos_status_t danos_tx_rollback_to(danos_tx_id_t target_tx_id);
```

### 23.3.3 回滚限制

| 场景 | 能否回滚 |
|------|---------|
| 配置变更 | ✓（WAL 有 inverse op） |
| FRR 路由变更 | 部分（需 FRR 配置回滚） |
| 接口 link up/down | ✗（物理状态不可回滚） |
| Backend 重连后状态 | 部分（Reconciler 重编程） |

## 23.4 镜像管理

### 23.4.1 OCI 镜像

| 属性 | 值 |
|------|---|
| 格式 | OCI image spec |
| 基础镜像 | Debian 12 / Ubuntu 24.04 |
| 层 | danos-core, danos-vpp, danos-fib, FRR, VPP, DPDK |
| 签名 | cosign（SigStore） |
| SBOM | CycloneDX / SPDX |

### 23.4.2 镜像验证

```bash
# 验证镜像签名
cosign verify --key cosign.pub ghcr.io/danos-open/danos:v0.1

# 提取 SBOM
cosign attach sbom --sbom danos-sbom.spdx ghcr.io/danos-open/danos:v0.1
```

### 23.4.3 镜像版本标签

| 标签 | 含义 |
|------|------|
| `v0.1.0` | 确切版本 |
| `v0.1` | v0.1 最新 patch |
| `latest` | 最新稳定版 |
| `main` | main 分支最新（不用于生产） |

## 23.5 兼容性

### 23.5.1 DPA API 版本协商

```c
// Backend 启动时上报 API 版本
danos_backend_info_t be = {
    .api_version = {0, 1, 0},
    ...
};
danos_backend_register(&be);

// Core 校验兼容性
if (be.api_version.major != DPA_MAJOR) {
    return DANOS_ERR_VERSION;
}
```

### 23.5.2 YANG 模型版本

- 每模块 `revision` 声明版本
- gNMI/NETCONF 客户端可查询支持的 revision
- 不兼容变更需 major 升级

### 23.5.3 配置兼容

| 变更类型 | 兼容性 |
|---------|--------|
| 新增配置节点 | 兼容（默认值） |
| 删除配置节点 | 不兼容（需 major 升级） |
| 改变默认值 | 兼容（但行为可能变化） |
| 改变语义 | 不兼容 |

## 23.6 升级检查清单

| 检查项 | 命令/方法 |
|--------|----------|
| 版本兼容 | `danos_dpa_get_version()` |
| Capability 兼容 | `danos_capability_query()` |
| YANG 模型兼容 | `pyang --compare` |
| 配置兼容 | dry-run commit |
| WAL 可重放 | `danos_wal_replay` 测试 |
| Conformance 通过 | `conformance_test` |

## 23.7 与现有实现的对齐

| 元素 | 代码位置 | 一致性 |
|------|---------|--------|
| Transaction ID | `dpa.h:33` (danos_tx_id_t) | §23.3 一致 |
| WAL 重放 | `wal.h:94` (danos_wal_replay) | §23.1.1 一致 |
| 版本协商 | `dpa.h:152` (danos_dpa_get_version) | §23.5.1 一致 |
| Capability 查询 | `dpa.h:168` (danos_capability_query) | §23.5.1 一致 |
| OCI 镜像 | `danos-build/oci/` | §23.4 一致 |
| Debian 包 | `danos-build/debian/` | §23.4 一致 |

## 23.8 状态

骨架文档（v1.1 P1-16）。实现待 v0.2+，依赖 `danos-ha/` 模块。
