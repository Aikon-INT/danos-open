# DPA API Specification v0.1

> 文件组成
> - `danos_dpa.h` — C ABI 头文件（进程内高性能调用）
> - `danos_dpa.proto` — Protobuf IDL + gRPC service（跨进程/跨语言）
> - `danos_dpa_spec.md` — 本文档：语义、错误码、版本、事务、并发模型

## 1. 三套等价接口

| 接口 | 用途 | 调用方 |
|------|------|--------|
| C ABI (`danos_dpa.h`) | 进程内最高性能 | DPA Core、Backend 插件（同进程） |
| Protobuf + gRPC (`danos_dpa.proto`) | 跨进程、跨语言 | FRR FIB Adapter、gNMI Server、Telemetry |
| YANG + NETCONF/gNMI | 管理面、人类可读 | CLI、NETCONF、gNMI、RESTCONF |

三套接口**语义等价**，对象字段一一对应。YANG 模型在 `danos-models/` 单独维护，本文档不展开。

## 2. 错误码语义

| 错误码 | 数值 | 触发场景 | 调用方应采取的行动 |
|--------|------|---------|-------------------|
| OK | 0 | 成功 | 继续 |
| INVALID_ARG | 1 | 参数校验失败（如 prefix_len > 128） | 修正参数重试 |
| NOT_FOUND | 2 | 读取/删除不存在的对象 | 检查上游是否已删除 |
| EXISTS | 3 | 创建已存在的对象 | 转为 update 或忽略 |
| NO_MEMORY | 4 | 后端内存不足 | 告警、不重试 |
| NO_CAPACITY | 5 | 超出后端声明的 max_count | 告警、不重试 |
| NOT_SUPPORTED | 6 | 后端 Capability 未声明此对象/特性 | 检查 Capability、换 backend 或降级 |
| PERMISSION | 7 | 授权失败 | 拒绝并审计 |
| TX_CONFLICT | 10 | 并发写事务冲突 | 重新 begin + 重放 |
| TX_TIMEOUT | 11 | 事务阶段超时 | abort 后重新 begin |
| TX_ABORTED | 12 | 事务被显式 abort | 重新 begin |
| TX_ROLLBACK | 13 | 回滚失败（需人工介入） | 告警、冻结对象、人工修复 |
| TX_INVALID | 14 | 事务状态非法（如已 commit 又 prepare） | 重新 begin |
| BACKEND_DOWN | 20 | 后端连接断开 | 等待重连、reconcile |
| BACKEND_BUSY | 21 | 后端暂时忙（如 VPP API 阻塞） | 退避重试 |
| BACKEND_IO | 22 | 后端 I/O 错误 | 告警、reconcile |
| VERIFY_FAIL | 30 | commit 后 verify 发现 Programmed ≠ Oper | 告警、reconcile、可能 rollback |
| PARTIAL | 31 | 批量操作部分成功 | 查 tx log、对失败项 reconcile |
| VERSION | 40 | API 版本不兼容 | 升级或降级 |
| CAPABILITY | 41 | 所需 Capability 未声明 | 检查 backend、降级 |
| INTERNAL | 99 | 内部 bug | 上报 issue、不重试 |

## 3. 版本协商规则

### 3.1 语义化版本

- `major`：ABI 破坏（结构体布局、枚举数值变更）
- `minor`：新增特性，向后兼容（新对象、新字段、新错误码）
- `patch`：Bug 修复

### 3.2 协商流程

```
Client                          Backend
  |                                |
  |--- GetVersion() -------------->|
  |<-- {major, minor, patch} ------|
  |                                |
  |--- QueryCapability(obj) ------>|
  |<-- {supported, max, features} -|
  |                                |
  |  if client.major != backend.major: VERSION error
  |  if client.minor >  backend.minor: degrade (skip new features)
  |  if capability not supported:    NOT_SUPPORTED / CAPABILITY error
```

### 3.3 兼容性承诺

- 同一 `major` 内，`minor`/`patch` 升级**不破坏**现有调用
- 新增字段追加在结构体末尾，旧客户端忽略
- 枚举新增值追加在末尾，旧客户端按 `UNKNOWN` 处理
- `major` 升级提供至少 1 个 `minor` 版本的兼容期（双版本共存）

## 4. 事务语义

### 4.1 状态机

```
                begin
   ──────────────────────────► [OPEN]
                                  │
                                  │ prepare
                                  ▼
                              [PREPARE]
                                  │
                          validate │ │ abort
                                  ▼ │
                             [VALIDATE] │
                                  │      │
                          commit  │      │
                                  ▼      ▼
                              [COMMIT] [ABORT] ──► (done)
                                  │
                          verify  │
                                  ▼
                              [VERIFY]
                                  │
                                  │ ok
                                  ▼
                               [DONE]
                                  │
                          rollback │ (best-effort)
                                  ▼
                             [ROLLBACK] ──► (done)
```

### 4.2 各阶段语义

| 阶段 | 语义 | 失败处理 |
|------|------|---------|
| OPEN | 分配 TxID，记录 initiator，无任何后端交互 | — |
| PREPARE | 将所有 op 暂存到后端 staging 区，不生效 | ABORT |
| VALIDATE | 校验 staging ops 是否符合 Capability 与语义规则 | ABORT |
| COMMIT | 原子提交 staging → programmed | ROLLBACK（若已部分提交则 PARTIAL） |
| VERIFY | 读取 oper，确认 == programmed | 告警 + Reconcile |
| DONE | 释放事务资源 | — |
| ABORT | 丢弃 staging | — |
| ROLLBACK | 对已 commit 的 op 执行 inverse，记录 WAL | 告警 + 人工介入 |

### 4.3 便捷接口

`danos_tx_commit_atomic(tx)` = `prepare + validate + commit + verify`，用于管理面单对象操作。失败时自动 `abort`（未 commit）或标记 `rollback`（已 commit）。

## 5. 并发模型

### 5.1 多读单写

- **读事务**：无锁，可并发任意数量。读取 Programmed 快照。
- **写事务**：全局有序，同一时刻仅一个写事务进入 PREPARE/COMMIT。
- 写事务排队等待超过 `prepare_ms` 返回 `TX_TIMEOUT`。

### 5.2 隔离级别

- **Read Committed**：读事务看到的是最近已 commit 的快照。
- 写事务之间**串行**（无并发写），天然避免脏写、写偏序。

### 5.3 超时

| 阶段 | 默认超时 | 可配置 |
|------|---------|--------|
| PREPARE | 100 ms | TxTimeouts.prepare_ms |
| COMMIT | 500 ms | TxTimeouts.commit_ms |
| VERIFY | 1000 ms | TxTimeouts.verify_ms |

超时后事务自动 ABORT（未 commit）或标记需 ROLLBACK（已 commit）。

### 5.4 持久化

- 每个写事务的 op 列表 + 结果写入 **WAL（Write-Ahead Log）**
- WAL 用于：HA 同步、审计、崩溃恢复（重放未 verify 的事务）
- WAL 保留策略：可配置（按时间或按数量）

## 6. Reconciliation 策略

### 6.1 触发方式

- **事件驱动**：后端 oper 变更通知 → 立即 reconcile 对应对象
- **周期对账**：默认 30s 全量 diff（可配置 `reconcile_period_ms`）
- **手动触发**：`danos_reconcile_trigger(obj_type)`

### 6.2 算法

```
for each obj_type in changed_types:
    desired = read_desired(obj_type)
    programmed = read_programmed(obj_type)
    diff = desired - programmed
    for obj in topological_sort(diff):
        retry_count = 0
        while retry_count < max_retries:
            status = reprogram(obj)
            if status == OK: break
            retry_count++
            sleep(min(backoff_initial * 2^retry_count, backoff_max))
        if retry_count == max_retries:
            mark_degraded(obj)
            emit_alert(obj)
```

### 6.3 防震荡

- 同一对象在 `antiflap_window_ms`（默认 5s）内最多重编程 `antiflap_max_count`（默认 3）次
- 超过则冻结该对象、告警、等待人工介入或窗口过期

### 6.4 收敛性

- 单调递增的 TxID 保证重编程不会回退到旧版本
- 假设后端最终响应（无永久故障），系统最终一致
- 若后端永久故障，对象标记 `degraded`，不无限重试

## 7. Capability 与 DPA Object 关系

### 7.1 形式化定义

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

### 7.2 示例

VPP backend 上报：
```json
{
  "OBJ_ROUTE":    {"supported": true, "max_count": 1000000, "features": ["ipv4", "ipv6", "multipath"]},
  "OBJ_EVPN":     {"supported": true, "max_count": 4096,    "features": ["type2", "type3", "type5", "irb", "mh"]},
  "OBJ_QOS":      {"supported": true, "max_count": 1024,    "features": ["policer-single-rate"], "constraints": {"max_rate_bps": 100000000000}},
  "OBJ_MULTICAST":{"supported": false}
}
```

OVS-DPDK backend 上报：
```json
{
  "OBJ_ROUTE":    {"supported": true, "max_count": 500000,  "features": ["ipv4", "ipv6", "multipath"]},
  "OBJ_EVPN":     {"supported": true, "max_count": 1024,    "features": ["type2", "type3"], "constraints": {"note": "no IRB in v0.3"}},
  "OBJ_QOS":      {"supported": false}
}
```

### 7.3 校验时机

1. **Backend 注册时**：Core 记录所有 backend 的 Capability Profile
2. **事务 VALIDATE 阶段**：Core 检查每个 op 是否被目标 backend 的 Capability 支持
3. **运行时 Capability 变更**：Backend 通过 `EVENT_CAPABILITY` 通知 Core，Core 重新评估

## 8. 事件与通知

### 8.1 事件类型

| 事件 | 触发 | 订阅方 |
|------|------|--------|
| OBJ_CREATED/UPDATED/DELETED | 对象变更 | gNMI Subscribe、telemetry |
| TX_COMMITTED/ROLLBACK | 事务完成 | 审计日志、HA 同步 |
| RECONCILE | Reconcile 执行 | 监控、告警 |
| BACKEND_DOWN/UP | 后端连接状态 | HA、告警 |
| CAPABILITY | Capability 变更 | Core 内部 |

### 8.2 投递语义

- **at-least-once**：事件可能重复，订阅方需幂等
- **有序**：同一对象的事件按 TxID 有序
- **非阻塞**：事件投递不阻塞事务主路径

## 9. ABI 稳定性承诺

- 结构体新增字段追加在**末尾**，不改变已有字段顺序
- 枚举新增值追加在**末尾**，不改变已有数值
- 函数新增在**末尾**，不改变已有签名
- 删除字段/函数/枚举值视为 `major` 版本升级
- 提供 `sizeof(danos_route_t)` 等结构体大小校验机制（编译期 + 运行期）
