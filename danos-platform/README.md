# danos-platform — 平台适配

> v0.2+ 计划模块。x86 / ARM / generic 平台抽象层。

## 职责

- **平台探测**：CPU 架构、NIC 型号、 Hugepage、CPU affinity
- **x86_64**：DPDK VFIO/UIO、RSS、NUMA 亲和
- **aarch64**：DPDK numa、SVE/SVE2 检测
- **generic**：纯软件回退（veth + raw socket，用于 CI/单测）
- **硬件资源管理**：Hugepage 分配、CPU pool、NIC 队列分配
- **平台能力上报**：经 DPA Capability 接口暴露

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。
现有支持：x86_64 与 aarch64 交叉编译已就绪（`cmake/aarch64.cmake` + CI）。

## 接口边界

- 与 `danos-vpp` / `danos-ovs` / `danos-p4`：提供平台资源句柄
- 与 `danos-build`：平台相关构建参数
