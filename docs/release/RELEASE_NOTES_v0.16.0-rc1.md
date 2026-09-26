# DANOS-Open v0.16.0-rc1

更新时间：2026-09-26

## 发布范围

本候选版本只覆盖最小 L3 NOS 软件闭环：管理面、DPA、FRR 10.3 ZAPI
和 VPP 26.10 FIB。协议扩展与真实 PCI 线速性能不属于本候选版本的已完成
范围。

## 已验证

- CTest 34/34 PASS。
- Route/NH/NHGroup backend contract、删除、回滚和 replay conformance PASS。
- topology-89：FRR/VPP/BGP/OSPF/ZAPI add、withdraw、restore、zserv 和 daemon
  重启恢复 PASS。
- QEMU e1000 双端口报文转发和 ECMP 功能 PASS。
- VMware VMXNET3 `no-rx-interrupts` polling-only 双网段报文基线 PASS：
  100/1000 包约 97–99 pps、0% 丢包。

## 明确边界与已知缺陷

- VMware polling-only 数字是 packet-level regression baseline，不是线速
  吞吐结论；没有发布吞吐、延迟或 CPU 性能数字。
- QEMU VMXNET3 DPDK 在 VPP 26.10 初始化后 SIGSEGV，已记录为已知缺陷。
- native VMXNET3 插件在当前虚拟拓扑中没有可用 MSI-X 中断线，接口不能作为
  通过的 DPDK lane。
- 真实 PCI DPDK 性能为 `ENVIRONMENT-OPEN`，等待独立 VFIO/uio、HugePages、
  VPP DPDK 和流量发生器 runner。
- BFD、VLAN、VXLAN、EVPN、OVS、P4 和平台适配不在本候选版本范围内。

## 验收入口

```sh
bash danos-test/integration/run_v016_release_gate.sh
```

硬件环境不可用时，gate 必须保留结构化 `SKIP` 或 `ENVIRONMENT-OPEN`，不得
将 QEMU 或 VMware polling-only 结果升级为真实 PCI 性能 PASS。
