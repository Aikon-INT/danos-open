# danos-p4 — P4 Backend

> v0.4+ 计划模块。P4Runtime / P4 backend 适配。
> 子项关系见 v1.1 §3.2。

## 职责

- **P4Runtime Adapter**：DPA → P4Runtime 控制面协议
- **BMv2 target**：软件 P4 target，仅用于功能测试与 CI（不用于生产）
- **P4-DPDK target**：生产 P4 target，基于 DPDK 的 P4 软件 dataplane
- **P4C 工具链**：编译器依赖，非运行时
- DPA 对象 → P4 表项映射
  - Interface → P4 extern
  - VRF → P4 metadata field
  - Route/NH → P4 lpm/exact table entry
  - ACL → P4 ternary table entry

## 优先级

1. P4Runtime + BMv2（v0.4 功能验证）
2. P4-DPDK（v0.5+ 生产）

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。实现待 v0.4+。

## 接口边界

- 实现 `danos-dpa` Backend 接口
- 与 `danos-core`：Capability 注册、Reconciler 反馈
- 外部依赖：P4Runtime、BMv2、P4-DPDK、P4C
