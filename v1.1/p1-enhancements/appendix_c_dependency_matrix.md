# 附录 C. 依赖版本兼容矩阵（v1.1 P1-15）

> v1.1 评审 §4.6 P1 改进动作。增强 `danos-build/deps/version_matrix.md`，
> 补充跨 DANOS 版本的兼容矩阵、ABI 兼容性、升级策略。

## 1. 跨版本兼容矩阵

| DANOS-Open | FRR | VPP | DPDK | OVS | P4Runtime | Linux | GCC | 状态 |
|-----------|-----|-----|------|-----|-----------|-------|-----|------|
| v0.1 | 10.2.x | 24.06.x | 23.11.x | - | - | 6.6+ | 13+ | 已发布 |
| v0.2 | 10.2.x | 24.06.x | 23.11.x | - | - | 6.6+ | 13+ | 开发中 |
| v0.3 | 10.2.x | 24.06.x | 23.11.x | 3.x | - | 6.6+ | 13+ | 规划 |
| v0.4 | 10.2.x | 24.06.x | 23.11.x | 3.x | 1.x | 6.6+ | 13+ | 规划 |
| v0.5+ | 10.x+ | 24.x+ | 23.x+ | 3.x+ | 1.x+ | 6.6+ | 13+ | 规划 |

## 2. ABI 兼容性

### 2.1 DPA C ABI（`danos_dpa.h`）

| DANOS 版本 | ABI 版本 | 兼容性 |
|-----------|---------|--------|
| v0.1 | 0.1.0 | 基线 |
| v0.2 | 0.2.0 | 向后兼容 v0.1（仅新增字段/枚举，不删除/重排） |
| v0.3 | 0.3.0 | 向后兼容 v0.2 |
| v1.0 | 1.0.0 | 可能 ABI break（major 升级） |

**ABI 稳定性承诺**（`dpa_spec.md`）：
- 结构体仅追加字段（尾部），不删除/重排
- 枚举仅追加值，不删除/重排
- 函数签名不变
- 新功能通过 Capability 协商，旧 Backend 忽略未知字段

### 2.2 Protobuf IDL（`dpa.proto`）

| 规则 | 说明 |
|------|------|
| 字段编号 | 永不重用，删除字段保留 `reserved` |
| 新字段 | 默认值保证旧客户端兼容 |
| 枚举 | 仅追加，不删除 |
| message | 不重命名，不改变语义 |

### 2.3 YANG 模型

| 规则 | 说明 |
|------|------|
| `revision` | 每次变更新增 revision 声明 |
| 节点 | 不删除，废弃用 `status deprecated` |
| 新节点 | 追加，默认值保证兼容 |
| 模块名 | 永不改变 |

## 3. 依赖升级策略

| 变更类型 | 策略 |
|---------|------|
| 依赖 patch 升级 | 自动接受（bug fix），CI 验证 |
| 依赖 minor 升级 | CI 矩阵测试通过后接受 |
| 依赖 major 升级 | 需 DANOS-Open minor 升级 + 全量 conformance 测试 |
| Linux 内核 | 跟随 LTS，最低 6.6 |
| DPDK | 跟随 LTS（23.11），容器化隔离版本 |

## 4. CI 矩阵测试

| 矩阵维度 | 值 |
|---------|---|
| OS | Ubuntu 24.04, Debian 12 |
| 架构 | x86_64, aarch64 |
| FRR | 10.2.x（单版本，锁定） |
| VPP | 24.06.x（单版本，锁定） |
| DPDK | 23.11.x（单版本，锁定） |
| GCC | 13, 14 |

## 5. 版本锁定文件

`danos-build/deps/versions.lock`：
```ini
FRR_VERSION=10.2.1
VPP_VERSION=24.06.0
DPDK_VERSION=23.11.1
LINUX_MIN_VERSION=6.6
GCC_MIN_VERSION=13
CMAKE_MIN_VERSION=3.16
```

## 6. 与现有文档的对齐

本文档增强 `danos-build/deps/version_matrix.md`，补充跨版本矩阵和 ABI 兼容性。
原文档的 v0.1 矩阵和兼容性规则保持不变。
