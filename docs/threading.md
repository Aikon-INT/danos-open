# DANOS-Open 线程模型(v0.8, mgrd)

> 本文是并发正确性的契约:列出每个线程、其触碰的共享状态、
> 保护机制。修改线程相关代码时必须同步更新本文。

## 线程清单

| 线程 | 来源 | 职责 | 触碰的共享状态 | 保护 |
|------|------|------|----------------|------|
| main | mgrd | 生命周期:persist→recover→启动各服务→pause | g_default_store(读写一次启动期)| 启动期单线程 |
| gNMI accept | gnmi_grpc_start | accept → 每连接派发线程 | — | — |
| gNMI conn ×N | grpc_conn_thread | HTTP/2 状态机、RPC dispatch、Set 写 store | g_default_store;prometheus(未来计数)| store rwlock;prom rwlock |
| /metrics accept | prom_server | accept → 每连接派发线程 | prometheus g_metrics | prom rwlock |
| /metrics conn ×N | prom_conn | danos_prom_render 快照 | g_metrics | prom rwlock(rd) |
| gRPC 子系统全局 | gnmi_grpc.c | `g_store`(仅 set_store 时写,启动期)| — | 启动期约定 |

## 锁规则

1. **object store rwlock**(object_registry.c):读迭代/读锁,写
   写锁。gNMI Set、CLI set、NETCONF edit-config、恢复路径全部经此。
2. **prometheus rwlock**(prometheus.c):g_metrics 表的注册/写值/
   绑定为写锁;render 为读锁。provider 回调(vpp stat 查询)在
   写锁内调用——provider 必须无阻塞、无再入 prometheus。
3. **禁止跨锁调用**:任何持 store 锁的路径不得再获取 prometheus
   锁,反之亦然(当前无此调用链,新增时须保持)。

## 已验证的并发场景

- 32 并发连接 × 4 RPC(concurrent_conns_test,plain + ASAN)
- gNMI 与 /metrics 并发(mgrd 冒烟)
- 已知串行点:gNMI 请求在单连接内顺序处理(多连接并行);
  事务提交的全局写锁(transaction.c)——性能基线显示
  0.001 ms/obj,非瓶颈。

## v0.9 增补

| reconciler 周期线程 | reconciler_start | programming_run(驱动 backend ops)| g_programmed 台账(私有)| 台账 store 自带 rwlock |

## v0.13 增补

| prometheus provider | /metrics render 时调用 | core 统计(programming/reconciler)与 VPP stat | prom rwlock 内调用——provider 必须无再入、无阻塞 |
| 事件回调(object_registry 发布) | gNMI/CLI/NETCONF 连接线程同步执行 | mark_dirty + store_epoch cond 广播 | 无锁(原子/短临界区)|

## 待扩展

- gNMI Subscribe STREAM 长连接占用其连接线程;若需单连接多订阅,
  需引入每流状态机(记录于 v0.9 候选)。
