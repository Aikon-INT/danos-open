# ADR-0007: Backend Adapter 管线与 PROGRAMMED 生命周期

状态:已接受 (2026-09-13)
关联:ADR-0002(VPP 主数据面)、ADR-0005(事务模型)

## 背景

v0.8 审查发现:状态模型(CONFIG→DESIRED→PROGRAMMED→OPER)右半边
空转——reconciler 统计"修复数"但从未调用任何后端,vpp_mapper 只有
测试调用。数据面与管理面之间缺最后一座桥。

## 决策

1. **引入 backend adapter 接口**(`danos-core/backend_ops.h`):
   `iface_up / route_add / route_del / vrf_add / vrf_del`,
   由各数据面后端实现(netlink、VPP、mock)。幂等要求:reconciler
   可能重复下发同一条目。
2. **PROGRAMMED 台账**:私有 object store,键 (type,id),值 = 内容
   哈希。desired 内容与台账一致 → in sync;不一致 → 重新下发
   (幂等)。崩溃安全:重启后台账在内存中重建,未编程项自动重发。
3. **reconciler 只说真话**:run_once() 调用 danos_programming_run(),
   stats 记录真实 attempt/failed;不再"假定修复成功"。
4. **第一个 adapter 是内核 netlink 后端**(mock + real rtnetlink 两
   模式),VPP mapper 随后以同一接口接入。kernel 后端的价值:离线
   可验证整条管线 + 容器部署形态。

## 范围(v0.9)

- 可编程对象:IFACE(admin state)、ROUTE(IPv4)、VRF(登记)。
  ACL/QoS/BFD/MPLS/EVPN/tunnel/multicast/NH 仍标记为"管线未覆盖",
  在 programming pass 中跳过。
- 删除追踪依赖 tombstone(对象删除事件),延后到 v0.10;当前 desired
  中消失的对象,后端条目保留(幂等重发无害)。

## 后果

- V 组验收(V1-V5)从"等环境"变为"等环境 + 已验证管线的接线"。
- 谎报修复的 reconciler 行为被根治;`total_failures` 有意义。
- 新后端 = 实现 5 个函数 + 注册,无需触碰 reconciler/store。
