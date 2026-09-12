# ADR-0006: BFD 会话由 FRR bfdd 承载,DANOS-Open 只做状态聚合

状态:已接受 (2026-09-13)
关联:ADR-0003(FRR 进程隔离)、danos-ha BFD 模块现状

## 背景

danos-ha 中有一个 ~100 行的自研 BFD 状态机雏形。RFC 5880 完整实现
(状态机、定时器微秒级精度、echo 模式、鉴别、多跳)工作量大且属于
"已被上游解决"的问题。

## 决策

1. **不自研完整 BFD 协议栈。** BFD 会话的收发、状态机、定时器由
   FRR bfdd 进程承载(与 zebra/bgpd 同一的隔离模型,通过 vtysh/
   bfdd 的northbound 配置)。
2. **danos-ha 的 bfd 模块重新定位为"BFD 会话管理器 + 状态聚合器":**
   - 把 DPA `DANOS_OBJ_BFD` 的 CRUD 翻译成对 bfdd 的配置(生成
     frr.conf 片段或经 FRR northbound)。
   - 订阅/轮询会话状态(up/down/admin-down),写回 DPA oper 状态,
     供 gNMI Subscribe 推送和联动模块(如 VRRP 抢占)消费。
   - 现有 ~100 行状态机保留为 mock 后端的语义模型,不进数据面。
3. **多跳(multihop)与单跳都委托 bfdd**;DPA 对象里的
   `multihop` 字段只影响生成的 bfdd 配置形态。

## 理由

- 协议正确性风险转移给上游专家实现;我们聚焦 DPA 抽象与编排。
- 与项目核心叙事一致:控制面协议进 FRR,我们做"软件优先"的集成层。
- 减少约 3000+ 行的协议代码与相应的 conformance 负担。

## 后果

- danos-ha/bfd 的 DoD 改为:能创建/删除/查询经 bfdd 的会话,状态
  变化 1s 内反映到 DPA oper store 与 gNMI 订阅者。
- 需要一个 bfdd 容器/进程可用;无 bfdd 环境时 mock 后端兜底。
- 若未来出现 FRR 无法覆盖的场景(如毫秒级 BFD for MPLS TP),
  再开新 ADR 评估自研。
