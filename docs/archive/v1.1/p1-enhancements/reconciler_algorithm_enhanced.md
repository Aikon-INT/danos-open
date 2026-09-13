# Reconciliation 算法增强（v1.1 P1-12）

> v1.1 评审 §4.5 P1 改进动作。明确触发机制、算法步骤、收敛性证明、
> 防震荡策略。与现有实现 `danos-core/src/reconciler/reconciler.c`、
> `danos-core/include/danos/core/antiflap.h` 一致。

## 1. 触发机制

| 触发源 | 时机 | 延迟 |
|--------|------|------|
| 事件驱动 | Oper 状态变更通知（`DANOS_EVENT_OBJ_UPDATED`） | < 10ms |
| 周期对账 | 每 `reconcile_period_ms`（默认 30s） | 30s |
| 显式触发 | `danos_reconcile_trigger(type)` | 立即 |
| Backend 重连 | `DANOS_EVENT_BACKEND_UP` | 立即全量对账 |
| Capability 变更 | `DANOS_EVENT_CAPABILITY` | 立即重校验受影响对象 |

**双触发设计**：事件驱动保证低延迟响应，周期对账兜底覆盖遗漏事件。

## 2. 算法

### 2.1 主流程

```
function reconcile(object_type):
    desired = state.get_desired(object_type)
    programmed = state.get_programmed(object_type)
    diff = compute_diff(desired, programmed)
    
    # 按依赖拓扑序排序（如 Route 依赖 NH/NHGroup 依赖 Interface）
    diff_sorted = topological_sort(diff)
    
    for obj in diff_sorted:
        if antiflap.is_flapping(obj.key):
            log("suppressed: %s is flapping", obj)
            continue
        
        if not antiflap.check_and_record(obj.key):
            log("marked flapping: %s", obj)
            emit_alert(obj)
            continue
        
        retry_count = 0
        while retry_count < config.max_retries:
            st = reprogram(obj)  # 提交到 backend
            if st == OK:
                state.set_programmed(obj)
                stats.total_repairs++
                break
            else:
                retry_count++
                backoff = min(config.backoff_initial_ms * 2^retry_count,
                              config.backoff_max_ms)
                sleep(backoff)
        
        if retry_count == config.max_retries:
            obj.degraded = true
            stats.total_failures++
            emit_alert(obj)
    
    stats.total_runs++
    stats.total_diffs += len(diff)
```

### 2.2 Diff 计算

```
function compute_diff(desired, programmed):
    diff = []
    for key in desired.keys():
        if key not in programmed:
            diff.append((CREATE, desired[key]))
        elif desired[key] != programmed[key]:
            diff.append((UPDATE, desired[key]))
    for key in programmed.keys():
        if key not in desired:
            diff.append((DELETE, programmed[key]))
    return diff
```

### 2.3 依赖拓扑序

对象间依赖：
- Route → NHGroup → NH → Interface
- ACL Rule → ACL Table → Interface
- QoS Policy → Interface
- BFD Session → Interface

重编程顺序：先创建被依赖对象，再创建依赖对象；删除反之。

## 3. 退避策略

| 重试次数 | 退避时间 |
|---------|---------|
| 1 | 1s（`backoff_initial_ms`） |
| 2 | 2s |
| 3 | 4s |
| 4 | 8s |
| 5 | 16s |
| ≥6 | 60s（`backoff_max_ms` 上限） |

**最大重试**：`max_retries`（默认 5），超过后标记 `degraded` 并告警。

## 4. 防震荡（Anti-Flap）

### 4.1 策略

| 参数 | 默认值 | 配置字段 |
|------|--------|---------|
| 窗口 | 5s | `antiflap_window_ms` |
| 最大修复次数 | 3 | `antiflap_max_count` |

**规则**：同一对象在 5s 窗口内被 Reconciler 修复超过 3 次，标记为 `flapping`，
自动修复被抑制，需人工介入（`antiflap_clear`）。

### 4.2 实现（`antiflap.h`）

```c
// 每对象记录修复历史（ring buffer 8 个时间戳）
typedef struct {
    uint64_t obj_key;
    uint64_t timestamps[8];
    uint32_t count;
    uint64_t window_start;
    bool     flapping;
} antiflap_entry_t;

// 检查并记录
bool antiflap_check_and_record(ctx, obj_key);
//  → 若窗口内 count >= max_count，返回 false（抑制）
//  → 否则记录时间戳，返回 true（允许）
```

### 4.3 震荡场景示例

| 场景 | 行为 |
|------|------|
| 链路频繁 up/down 导致 Route 反复重编程 | 5s 内 > 3 次 → 冻结 Route，告警 |
| Backend 短暂故障导致 VERIFY 反复失败 | 退避重试，不立即触发防震荡 |
| 配置错误导致对象无法编程 | 退避至 max_retries，标记 degraded |

## 5. 收敛性证明

**定理**：假设 Backend 最终响应（即无限重试最终成功），Reconciler 保证最终一致：
`lim(t→∞) Programmed = Desired`

**证明**：
1. **单调性**：每次成功 reprogram 使 `|Desired - Programmed|` 严格递减
2. **有限性**：Desired 和 Programmed 都是有限集，diff 大小有限
3. **终止性**：防震荡冻结有限对象，其余对象按退避重试，由假设最终成功
4. **结论**：有限步内 diff → 0，即 Programmed = Desired

**版本单调性**：每个对象携带单调递增版本号，Reconciler 只处理版本号递增的变更，
避免旧事件覆盖新状态。

## 6. 配置参数（`dpa.h:554-561`）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `reconcile_period_ms` | 30000 | 周期对账间隔 |
| `max_retries` | 5 | 单对象最大重试 |
| `backoff_initial_ms` | 1000 | 初始退避 |
| `backoff_max_ms` | 60000 | 最大退避 |
| `antiflap_window_ms` | 5000 | 防震荡窗口 |
| `antiflap_max_count` | 3 | 窗口内最大修复次数 |

## 7. 统计（`dpa.h:567-573`）

| 指标 | 含义 |
|------|------|
| `total_runs` | Reconcile 总运行次数 |
| `total_diffs` | 发现的 diff 总数 |
| `total_repairs` | 成功修复总数 |
| `total_failures` | 修复失败总数（达 max_retries） |
| `total_flaps` | 触发防震荡总数 |

## 8. 与现有代码的对齐

| 元素 | 代码位置 | 一致性 |
|------|---------|--------|
| 默认配置 | `reconciler.c:14-21` | §6 一致 |
| Diff 回调 | `reconciler.c:54-59` | §2.2 一致 |
| 防震荡接口 | `antiflap.h:56-73` | §4 一致 |
| 配置结构 | `dpa.h:554-561` | §6 一致 |
| 统计结构 | `dpa.h:567-573` | §7 一致 |

## 9. 测试覆盖

| 测试 | 覆盖 | 代码位置 |
|------|------|---------|
| `core` | Reconciler 骨架 + 事件触发 | `danos-core/tests/` |
| `antiflap` | 防震荡窗口 + 冻结 + 告警 | `danos-core/tests/` |
