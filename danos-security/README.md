# danos-security — 安全

> v0.2+ 计划模块。AAA / TLS / key management / CoPP。
> 设计依据：`v1.1/security/security_architecture.md`（第 20 章）。

## 职责

- **认证 (AAA)**：本地用户、TACACS+、RADIUS，fallback 链
- **传输安全**：SSH（CLI/NETCONF）、TLS（RESTCONF）、mTLS（gNMI）
- **授权 (RBAC)**：角色-权限模型，命令/路径级控制
- **控制面安全**：BGP TTL Security (GTSM)、TCP-AO、Keychain
- **数据面 CoPP**：Control Plane Policing，分类限速
- **数据面 ACL**：默认 deny 策略
- **密钥管理**：集中式 KMS、密钥轮换、证书轮换
- **审计**：所有 Transaction 记录主体/时间/diff

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。设计文档已完成，实现待 v0.2+。

## 接口边界

- 与 `danos-mgmt`：所有管理面接入认证/授权/传输安全
- 与 `danos-core`：Transaction 审计钩子
- 与 `danos-vpp`：CoPP / ACL 下发
