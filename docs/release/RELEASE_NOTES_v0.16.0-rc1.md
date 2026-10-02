# DANOS-Open v0.16.0-rc1

更新时间：2026-10-01

## 发布范围

本候选版本只覆盖最小 L3 NOS 软件闭环：管理面、DPA、FRR 10.3 ZAPI
和 VPP 26.10 FIB。协议扩展与真实 PCI 线速性能不属于本候选版本的已完成
范围。

## 已验证

- CTest 34/34 PASS。
- Route/NH/NHGroup backend contract、删除、回滚和 replay conformance PASS。
- topology-128（clean source commit `c632429`）：FRR/VPP/BGP/OSPF/ZAPI add、withdraw、restore、zserv 与 daemon
  重启恢复通过；VPP restart/replay 通过。
- QEMU e1000 双端口基础报文、4 目的多流 ECMP 和双 bucket 计数通过；显式撤销一条
  ECMP 下一跳后四个探测目的均经存活 bucket 5/5 转发，恢复下一跳后双 bucket 重建，
  四个目的均 5/5 恢复。
- 边界：仅将接口 admin-down 而保留静态 route/NH 时，VPP 26.10 仍保持该路由 bucket；
  验收使用实际 route/NH withdrawal，不把单独的接口状态变化误报为路由撤销。
- QEMU e1000/DPDK integration ISO：`build/danos-vpp-dpdk-e1000-2port-frr-ecmp-ready-r23.iso`；
  SHA256 `4570819432a1ddc1f3d4d0cd15783a4ce45e3330c24ed5d5ac7e22e8181148a6`，build
  identity `source_dirty=0`。
- VMware VMXNET3 `no-rx-interrupts` polling-only 双网段 packet baseline：clean
  commit `b9c4020` 冷启动；1000 包/路径、2000/2000 收发、0% 丢包，98.23 pps，
  p50/p99 206/449 µs，ECMP bucket 计数 9/1003。此为低速 ICMP 回归数据，不是线速
  或 ECMP 均衡性能结论。ISO SHA256 `1ba4a1516a005741fd691809450b25ef6433807449ccc2619345e28490672d1b`。
- I211 live ISO 加入 USB host-controller 和 HID keyboard modules；QEMU xHCI
  验证键盘枚举，并通过 USB keyboard 输入命令至 tty1 shell；可用
  `danos-test/live/verify_usb_keyboard_qemu.py` 重复验收。

## 明确边界与已知缺陷

- VMware polling-only 数字是 packet-level regression baseline，不是线速
  吞吐结论；没有发布吞吐、延迟或 CPU 性能数字。
- QEMU VMXNET3 DPDK 在 VPP 26.10 初始化后 SIGSEGV，已记录为已知缺陷。
- native VMXNET3 插件在当前虚拟拓扑中没有可用 MSI-X 中断线，接口不能作为
  通过的 DPDK lane。
- 真实 PCI DPDK 性能为 `ENVIRONMENT-OPEN`，等待独立 VFIO/uio、HugePages、
  VPP DPDK 和流量发生器 runner。
- 当前完整 gate 的总体功能状态为 PASS；PCI runner/线速性能仍为
  `ENVIRONMENT-OPEN`，不构成 v0.16.0 真实硬件性能资格。
- 实机 I211 USB 输入/冷启动在本环境尚无串口证据；QEMU HID 键盘测试不能代替实机验收。
- 最新 I211 USB hybrid ISO：`build/danos-open-v0.16.0-rc1-i211-dpdk-usb-hid-final.iso`；
  SHA256 `ebb3fc3418d22c4db55e7c5b9114ca3754e685bf3c039e11fcfbb0f13b64006a`。
- BFD、VLAN、VXLAN、EVPN、OVS、P4 和平台适配不在本候选版本范围内。

## 验收入口

```sh
bash danos-test/integration/run_v016_release_gate.sh
```

硬件环境不可用时，gate 必须保留结构化 `SKIP` 或 `ENVIRONMENT-OPEN`，不得
将 QEMU 或 VMware polling-only 结果升级为真实 PCI 性能 PASS。
