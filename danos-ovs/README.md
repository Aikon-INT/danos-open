# danos-ovs — OVS-DPDK Backend

> v0.3+ 计划模块。OVS-DPDK 数据面 backend，经 DPA 适配。

## 职责

- DPA 对象 → OVS-DPDK 流表映射
  - Interface → OVS interface
  - VRF → OVS bridge / flow table
  - Route/NH → OVS flow rules
  - ACL → OVS flow rules (priority)
- OVSDB 集成：经 OVSDB 协议下发配置
- DPDK 接口管理：vHost User、DPDK port
- Capability 上报：OVS-DPDK backend 真实能力

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。实现待 v0.3+。

## 接口边界

- 实现 `danos-dpa` Backend 接口
- 与 `danos-core`：Capability 注册、Reconciler 反馈
- 外部依赖：OVS-DPDK 3.x
