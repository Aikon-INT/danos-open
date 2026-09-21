# DANOS-Open 待办清单(v0.15 收敛基线)

> 当前状态、版本路线和验收口径见 `docs/project-status.md`。本清单只保留尚未完成的可执行事项。

## v0.15 工程收敛
- [ ] 统一 README、TODO、release notes、tag 与构建产物版本口径
- [ ] 将当前 gNMI 修复拆分提交并完成协议回归
- [ ] 完成 Route/NH/NHGroup 依赖删除、重试和 tombstone 验收
- [x] 增加 Interface IPv4/IPv6 地址模型及 northbound 测试（Linux backend 已支持；VPP 地址消息仍待）
- [x] 增加特权集成测试 lane，并区分环境阻塞与代码失败（K4/K5 lane 已存在；本地缺少权限）
- [x] 增加 TSAN 门禁和正式 fuzz target（TSAN CMake/CI 配置及 gNMI 并发验证已完成；libFuzzer target/CI 已加入，当前工作区未安装 clang）
- [x] 完成真实磁盘 WAL fsync 基线（支持 `DANOS_WAL_BENCH_PATH`，CI 使用 `/var/tmp`）
- [x] 固定并验证 VPP runtime 版本（Debian trixie 源码构建，VPP 26.10-rc0~545-gad99177fe；mlx4/mlx5 disabled）

## 环境/特权依赖
- [x] K5-ping:root 容器中完成双 namespace 真实报文转发
      (run_v0.10_kernel.sh 的 K5 部分;沙箱 userns 拒绝对 moved veth 加地址)
- [x] VPP runtime/API/stat/J5 基线验收（有网络的 Debian trixie 特权容器）
      `run_v0.4_interop.sh --with-vpp`;adapter 已就位
      (danos_backend_ops "vpp",v0.11)
- [x] 真实 VPP CLI 双路径 ECMP FIB 安装/核查/撤销
- [x] 修复 VPP 26.10 binary-API socket framing/handshake，完成 API 驱动 ECMP 路由增删验收
- [x] VPP packet-level 双 namespace/af_packet/ICMP 转发验收

## 功能
- [x] v0.16 ECMP northbound route representation（gNMI `gateways[]` + NHGroup；真实 dataplane 多路径仍待）
- [x] FRR trixie BGP EVPN 邻居与 OSPF 邻居特权拓扑基线验收
- [ ] v0.16 FRR BGP/OSPF → ZAPI → DPA → backend route install/withdraw 全链路验收
- [x] 真实 FRR zebra ZAPI socket 可达性验收（V5）
- [x] 增加可运行的 danos-fib daemon wiring，完成 live ZAPI → DPA → VPP route lifecycle 入口（`fib_live_bridge`；需特权拓扑验收）
- [x] 3 节点 BGP/ECMP、OSPF 收敛与 ping 验收（F1/F2）
- [x] WAL/DPA 重启恢复与 1000-object 事务规模验收
- [x] 特权容器软件转发基线记录（0.4363 Mpps；VPP+DPDK 性能仍需专用 dataplane lane）
- [ ] VPP+DPDK 专用 lane（已固化 preflight；当前 runtime 未加载 DPDK 且无 PCI dataplane device）

### 下一阶段执行顺序
- [ ] 在全新 Debian trixie 特权 FRR+VPP 拓扑运行 `fib_live_bridge`，验收 route add/replace/ECMP/withdraw/traffic
- [ ] 增加 ZAPI client registration、重连、VPP 重连、信号处理、计数器和 restart recovery
- [ ] 完成 Route/NH/NHGroup 依赖删除、tombstone、retry、rollback 的 Linux/VPP 双 backend 验收
- [ ] 在具备 PCI/VFIO、hugepages 和 DPDK plugin 的专用 runner 执行 VPP+DPDK 64B 单核/多核性能验收
- [ ] v0.16 收敛前冻结 OVS/P4、BFD 及大范围模型扩展，避免主链路分散
- [ ] gNMI Subscribe STREAM 多订阅状态机(单连接多流;
      threading.md 已列为候选)
- [ ] bfdd 翻译层(ADR-0006):DPA BFD 对象 ↔ frr bfdd 配置
- [ ] 墓碑覆盖扩展:NHGroup/NH 对象从 desired 清除时的级联撤回
- [ ] 模型表直接从 YANG 生成(当前经 JSON 规范中转)
- [ ] NETCONF over SSH(RFC 6242 传输层)

## 质量
- [x] libFuzzer 集成 target/CI（本地未安装 clang）
- [x] TSAN 门禁（32 并发连接用例）
- [x] 真实磁盘(非 tmpfs)上的 WAL fsync 基线（CI `/var/tmp`）
