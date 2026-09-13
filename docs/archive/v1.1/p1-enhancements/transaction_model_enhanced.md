# 事务模型增强（v1.1 P1-11）

> v1.1 评审 §4.4 P1 改进动作。补充并发控制、隔离级别、超时、嵌套、持久化、
> 回滚策略。与现有实现 `danos-core/src/transaction/transaction.c`、
> `danos-core/include/danos/core/transaction.h`、`wal.h` 一致。

## 1. 状态机

```
OPEN → PREPARE → VALIDATE → COMMIT → VERIFY → DONE
                                    ↓
                                  ABORT
                                    ↓
                                ROLLBACK
```

| 状态 | 允许操作 | 不允许操作 |
|------|---------|-----------|
| OPEN | stage op, prepare, abort | validate, commit, verify |
| PREPARE | validate, abort | stage op, commit |
| VALIDATE | commit, abort | stage op, prepare |
| COMMIT | verify | abort（已不可逆） |
| VERIFY | done | - |
| DONE | - | - |
| ABORT | - | - |
| ROLLBACK | - | - |

非法状态转换返回 `DANOS_ERR_TX_INVALID`。

## 2. 并发控制

### 2.1 策略：多读单写

| 维度 | 实现 |
|------|------|
| 读事务 | 无锁，直接读 Programmed/Oper 状态快照 |
| 写事务 | 全局 `write_lock` 互斥（`transaction.h:31`），单 writer 串行 |
| 读写隔离 | 写事务在 COMMIT 前不改变 Programmed 状态，读事务看不到中间态 |

### 2.2 写事务串行保证

```c
// transaction.c:30
pthread_mutex_init(&g_tx_mgr->write_lock, NULL);

// COMMIT 阶段获取 write_lock
pthread_mutex_lock(&g_tx_mgr->write_lock);
// ... apply staged ops to Programmed ...
pthread_mutex_unlock(&g_tx_mgr->write_lock);
```

**性能影响**：写事务串行，但单事务可批量提交多个 op（批量 VPP API 调用），
吞吐率 4.7M tx/s（H1 基线）。

### 2.3 并发冲突处理

| 场景 | 处理 |
|------|------|
| 两个写事务同时 begin | 都成功 begin，COMMIT 时串行 |
| 写事务 A commit 期间，写事务 B commit | B 等待 write_lock，A 完成后 B 执行 |
| 读事务读期间写事务 commit | 读事务读的是快照，不受影响 |
| 超时 | 等待 write_lock 超时返回 `DANOS_ERR_TX_TIMEOUT` |

## 3. 隔离级别

**Read Committed + 写事务原子可见**

| 属性 | 保证 |
|------|------|
| 读已提交 | 读事务只看到已 COMMIT 的状态 |
| 无脏读 | 写事务的中间态（PREPARE/VALIDATE）不可见 |
| 无不可重复读 | 同一读事务内多次读同一对象结果一致（快照） |
| 幻读 | 可能（新提交的事务新增对象），由 Reconciler 处理 |
| 写原子性 | 单事务内所有 op 要么全部 COMMIT，要么全部 ABORT |

**不提供**：Serializable（性能代价过高，网络操作系统不需要）。

## 4. 超时

| 阶段 | 默认超时 | 可配置 | 超时动作 |
|------|---------|--------|---------|
| PREPARE | 100ms | `tx_timeouts.prepare_ms` | ABORT，返回 `TX_TIMEOUT` |
| COMMIT | 500ms | `tx_timeouts.commit_ms` | ABORT + ROLLBACK，返回 `TX_TIMEOUT` |
| VERIFY | 1000ms | `tx_timeouts.verify_ms` | 告警 + Reconcile，返回 `TX_TIMEOUT` |

**超时实现**（`transaction.c`）：
- 每个 Transaction 记录 `deadline_ns = start_ns + phase_timeout`
- 每阶段入口检查 `now_ns() > deadline_ns`
- VPP API 调用使用带超时的 recv（poll with timeout）

## 5. 嵌套事务

**不支持嵌套事务。**

- 子操作扁平化为单事务的多个 op
- 理由：网络配置的原子性单位是"一组相关对象变更"（如路由+NH+NHGroup），
  扁平化已足够；嵌套事务增加复杂度且无明确收益
- 如需分阶段提交，使用多个独立 Transaction + Reconciler 保证最终一致

## 6. 持久化（WAL）

### 6.1 WAL 格式（`wal.h:10-12`）

```
[magic:4][tx_id:8][op_type:1][obj_type:2][obj_id:4][data_len:4][data:N][crc:4]
```

### 6.2 持久化保证

| 时机 | 动作 |
|------|------|
| COMMIT 前 | 所有 op 写入 WAL（`wal_append`） |
| COMMIT 时 | 写 COMMIT marker + `fsync`（`wal_sync`） |
| ABORT 时 | 写 ABORT marker（不 fsync） |
| 崩溃恢复 | `wal_replay` 重放已 COMMIT 但未 checkpoint 的事务 |
| Checkpoint | 状态快照后 `wal_checkpoint` 截断 WAL |

### 6.3 用途

1. **崩溃恢复**：重启后重放未完成事务（B6 验收）
2. **HA 同步**：WAL 流式同步到备用路由引擎（v0.2+ HA）
3. **审计**：Transaction ID + op + diff 永久归档（v0.2+ 审计）

## 7. 回滚策略

| 场景 | 策略 |
|------|------|
| PREPARE/VALIDATE 阶段失败 | ABORT，丢弃 staged ops（无副作用） |
| COMMIT 阶段部分失败 | ROLLBACK：按逆序执行 inverse op |
| ROLLBACK 失败 | 返回 `DANOS_ERR_TX_ROLLBACK`，冻结对象，告警，人工介入 |
| VERIFY 失败 | 不自动 ROLLBACK，触发 Reconciler 重编程 |

**程序化回滚**（inverse op）：
- CREATE 的 inverse 是 DELETE
- UPDATE 的 inverse 是 UPDATE（恢复旧值）
- DELETE 的 inverse 是 CREATE（恢复旧对象）

**状态重编程 fallback**：若 inverse op 失败，Reconciler 从 Desired 状态全量重编程。

## 8. 与现有代码的对齐

| 元素 | 代码位置 | 一致性 |
|------|---------|--------|
| 状态机 | `dpa.h:176-185` (TxState 枚举) | §1 一致 |
| 并发控制 | `transaction.h:31` (write_lock) | §2 一致 |
| 超时 | `dpa.h:197-201` (TxTimeouts) | §4 一致 |
| WAL | `wal.h` | §6 一致 |
| 事务管理器 | `transaction.c:25-54` | §2/§4 一致 |

## 9. 性能基线

| 指标 | 值 | 来源 |
|------|---|------|
| 事务吞吐 | 4.7M tx/s | H1 perf_baseline 测试 |
| 单事务延迟 | < 1μs（无 I/O） | mock 模式 |
| WAL fsync 延迟 | ~1ms（SSD） | B6 测试 |
| 并发写冲突率 | 0（单 writer 串行） | B4 ThreadSanitizer 通过 |
