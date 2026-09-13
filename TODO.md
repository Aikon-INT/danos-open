# DANOS-Open 待办清单(v0.11 基线)

> 长期事项在这里公开追踪;阶段内工作见各 release notes。

## 环境/特权依赖
- [ ] K5-ping:root 容器中完成双 namespace 真实报文转发
      (run_v0.10_kernel.sh 的 K5 部分;沙箱 userns 拒绝对 moved veth 加地址)
- [ ] VPP 真实互通(V1-V5):有网络的机器上
      `run_v0.4_interop.sh --with-vpp`;adapter 已就位
      (danos_backend_ops "vpp",v0.11)

## 功能
- [ ] gNMI Subscribe STREAM 多订阅状态机(单连接多流;
      threading.md 已列为候选)
- [ ] bfdd 翻译层(ADR-0006):DPA BFD 对象 ↔ frr bfdd 配置
- [ ] 墓碑覆盖扩展:NHGroup/NH 对象从 desired 清除时的级联撤回
- [ ] 模型表直接从 YANG 生成(当前经 JSON 规范中转)
- [ ] NETCONF over SSH(RFC 6242 传输层)

## 质量
- [ ] libFuzzer 集成(当前为内建变异循环)
- [ ] TSAN 门禁(32 并发连接用例)
- [ ] 真实磁盘(非 tmpfs)上的 WAL fsync 基线
