# danos-ha — 高可用

> v0.2+ 计划模块。NSR / VRRP / EVPN-MH / supervisor / ISSU。
> 设计依据：`docs/archive/v1.1/ha/ha_design.md`（第 21 章）。

## 职责

- **NSR / Graceful Restart**：与 FRR `nsr` 集成，控制面不中断转发
- **VRRP / M-VRRP**：多主网关冗余
- **EVPN-MH**（RFC 7432 §8）：ESI 自动化、Alias/Per-EVI AD route
- **BFD**：多跳/单跳、微秒级链路检测
- **进程级 HA**：supervisor、进程重启策略、热补丁
- **节点级 HA**：双路由引擎、状态同步、ISSU

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。设计文档已完成，实现待 v0.2+。

## 接口边界

- 与 `danos-core`：HA 事件经事件总线订阅/发布
- 与 `danos-fib`：FRR NSR 状态同步
- 与 `danos-vpp`：数据面状态快照/恢复
