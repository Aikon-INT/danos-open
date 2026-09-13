# 第 20 章 安全架构

> v1.1 新增章节。安全不能后补，管理面 TLS/mTLS + AAA、CoPP、Transaction 审计日志
> 应进入 v0.1 或最迟 v0.2。

## 20.1 威胁模型

DANOS-Open 作为网络操作系统，面临以下威胁面：

| 威胁面 | 攻击向量 | 影响 | 缓解章节 |
|--------|---------|------|---------|
| 管理面 | 未授权 CLI/NETCONF/gNMI 访问 | 配置篡改、信息泄露 | §20.2 认证、§20.3 授权 |
| 管理面传输 | 中间人窃听/篡改 | 凭据泄露、配置泄露 | §20.4 传输安全 |
| 控制面 | BGP/OSPF 伪造/重放 | 路由劫持、黑洞 | §20.5 控制面安全 |
| 数据面 CoPP | 控制面流量风暴 | CPU 耗尽、DoS | §20.6 CoPP |
| 数据面 ACL | 未授权流量穿越 | 策略绕过 | §20.7 默认拒绝 |
| 密钥管理 | 密钥泄露/过期 | 后续所有安全失效 | §20.8 密钥管理 |
| 审计 | 操作不可追溯 | 事后无法追责 | §20.9 审计 |

## 20.2 认证 (Authentication)

### 20.2.1 管理面认证

| 接口 | 认证方式 | 默认 |
|------|---------|------|
| CLI (SSH) | SSH public key + 密码（可选） | 强制 public key |
| NETCONF | SSH 通道 + AAA | 强制 |
| RESTCONF | HTTP Basic + TLS | 强制 TLS |
| gNMI | mTLS（双向证书） | 强制 mTLS |

### 20.2.2 AAA 集成

- **本地用户**：`/etc/danos/users`，密码哈希（bcrypt/argon2）
- **TACACS+**：集中认证，支持多服务器、fallback
- **RADIUS**：集中认证，支持多服务器、fallback
- 优先级：本地 → TACACS+ → RADIUS

### 20.2.3 证书认证

- 管理面证书：X.509，ECDSA 优先
- 证书轮换：自动轮换周期可配置（默认 90 天）
- 证书吊销：CRL / OCSP 检查

## 20.3 授权 (Authorization)

### 20.3.1 RBAC 模型

```
Role → Permissions → (ObjectType, Operation)
```

预置角色：
| 角色 | 权限 |
|------|------|
| admin | 所有对象所有操作 |
| operator | 读所有 + 写非安全对象 |
| viewer | 只读 |
| security-admin | 读所有 + 写安全相关（AAA/密钥/CoPP） |

### 20.3.2 操作粒度

- 每个事务携带 `initiator`（用户/服务身份）
- DPA Core 在 VALIDATE 阶段校验权限
- 权限拒绝返回 `DANOS_ERR_PERMISSION` 并审计

## 20.4 传输安全

### 20.4.1 TLS/mTLS 配置

| 参数 | 默认值 | 可配置 |
|------|--------|--------|
| 最低 TLS 版本 | 1.3 | 是（不建议降级） |
| 密码套件 | TLS_AES_256_GCM_SHA384 | 是 |
| 证书签名算法 | ECDSA P-256 | 是 |
| 会话超时 | 1 小时 | 是 |

### 20.4.2 SSH 配置

| 参数 | 默认值 |
|------|--------|
| 最低 SSH 版本 | 2.0 |
| 禁用算法 | rsa-sha1, diffie-hellman-group1 |
| 登录超时 | 60s |
| 最大认证尝试 | 3 |

## 20.5 控制面安全

### 20.5.1 BGP 安全

- **TTL Security (GTSM)**：eBGP TTL=255，丢弃 TTL < 255 的包
- **TCP-AO**：替代 MD5，支持密钥轮换（RFC 5925）
- **Keychain**：FRR keychain 集成，按时间自动切换密钥
- **BGP RPKI**：路由源验证，拒绝 ROA 不匹配的路由
- **Max Prefix**：per-peer max-prefix 限制，超限 shutdown 或 warning

### 20.5.2 OSPF/IS-IS 安全

- **认证**：MD5（兼容）→ SHA-256（推荐）
- **GTSM**：TTL 限制（OSPF TTL=1, IS-IS TTL=1）

### 20.5.3 BFD 安全

- **认证**：SHA-1（兼容）→ SHA-256（推荐）

## 20.6 控制面策略 (CoPP)

### 20.6.1 目标

保护 CPU/控制平面免受流量风暴冲击。

### 20.6.2 实现

- VPP：classify + policer，将上 CPU 的流量分类限速
- 分类：
  | 类别 | 限速 | 说明 |
  |------|------|------|
  | BGP | 10 Kpps | 已知 neighbor |
  | OSPF/IS-IS | 10 Kpps | |
  | BFD | 50 Kpps | 高频但小包 |
  | SSH/NETCONF/gNMI | 1 Kpps | 管理面 |
  | ARP | 5 Kpps | |
  | ICMP | 2 Kpps | |
  | 未知 | 100 pps | 默认严格限速 |

### 20.6.3 配置

CoPP 策略通过 YANG 模型配置，默认启用安全策略。

## 20.7 数据面默认拒绝

- ACL 默认行为：**deny all**（未匹配的流量默认丢弃）
- 管理面访问 ACL：默认仅允许配置的管理 IP/网段
- 接口默认状态：admin-down（需显式 enable）

## 20.8 密钥管理

### 20.8.1 密钥类型

| 密钥 | 用途 | 存储 |
|------|------|------|
| SSH host key | SSH server | 本地文件，权限 600 |
| 管理面证书私钥 | TLS/mTLS | 本地文件或 KMS |
| BGP keychain | TCP-AO | 加密存储 |
| OSPF/IS-IS key | 协议认证 | 加密存储 |
| BFD key | BFD 认证 | 加密存储 |

### 20.8.2 KMS 集成

- 支持外部 KMS（HashiCorp Vault、PKCS#11 HSM）
- 密钥轮换：按周期或按事件
- 密钥永不以明文出现在日志/telemetry/审计

## 20.9 审计

### 20.9.1 审计日志内容

每条审计记录包含：
- Transaction ID
- 时间戳（UTC，纳秒精度）
- 操作主体（用户/服务）
- 操作类型（create/update/delete/commit/rollback）
- 对象类型与 ID
- 变更 diff（before/after）
- 结果（success/failure + error code）
- 来源 IP

### 20.9.2 审计日志保护

- 追加只写（append-only）
- 远程同步（syslog / gRPC stream）
- 防篡改：哈希链或签名
- 保留策略：默认 365 天

## 20.10 安全章节 MVP 对齐

| 特性 | v0.1 | v0.2 |
|------|------|------|
| SSH public key 认证 | ✅ | |
| RBAC 基础角色 | ✅ | |
| TLS 1.3 管理面 | ✅ | |
| BGP TTL Security | ✅ | |
| CoPP 基础策略 | | ✅ |
| TCP-AO | | ✅ |
| RPKI | | ✅ |
| KMS 集成 | | ✅ |
| 审计日志完整 | ✅ | |
