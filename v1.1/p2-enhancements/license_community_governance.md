# 第 25 章 许可证与社区治理（v1.1 P2-18）

> v1.1 评审 §2.1 第 25 章 P2 改进动作（v0.4 前完成）。
> 许可证选择、CLA/DCO 策略、贡献流程、商标与品牌、基金会关系。

## 25.1 许可证选择

### 25.1.1 核心许可证：Apache-2.0

DANOS-Open 核心代码（`danos-core`、`danos-dpa`、`danos-mgmt`、`danos-models`、
`danos-vpp`、`danos-observability`、`danos-security`、`danos-ha`）采用
**Apache License 2.0**。

选择理由：
1. **商业友好**：允许闭源衍生作品，鼓励厂商采用
2. **专利授权**：明确专利授权条款，降低专利风险
3. **生态兼容**：与 VPP（Apache-2.0）、DPDK（BSD-3-Clause）兼容
4. **行业标准**：ONF、LF Networking 主流 NOS 多采用 Apache-2.0

### 25.1.2 依赖许可证兼容性矩阵

| 组件 | 许可证 | 兼容性 | 集成方式 |
|------|--------|--------|---------|
| FRR | GPLv2 | **不兼容**（传染性） | 进程隔离（独立进程 + ZAPI 通信） |
| VPP | Apache-2.0 | 兼容 | 库链接 |
| DPDK | BSD-3-Clause | 兼容 | 库链接 |
| OVS | Apache-2.0 | 兼容 | 库链接（v0.3+） |
| P4Runtime | Apache-2.0 | 兼容 | gRPC（v0.4+） |
| Linux kernel | GPLv2 | 不传染（系统调用例外） | 系统调用 |
| protobuf | BSD-3-Clause | 兼容 | 代码生成 |
| gNMI/gNOI | BSD-3-Clause | 兼容 | 协议实现 |

### 25.1.3 FRR GPLv2 隔离策略

FRR 采用 GPLv2，与 Apache-2.0 **不兼容**。DANOS-Open 通过以下方式隔离：

1. **进程隔离**：FRR 作为独立进程运行，DANOS-Open 不链接 FRR 代码
2. **通信协议**：通过 ZAPI（Zebra API）socket 通信，属系统调用例外
3. **配置隔离**：FRR 配置由 DANOS-Open 生成，不包含 FRR 代码
4. **法务确认**：需法务团队确认进程隔离方案符合 GPLv2 条款

```
┌─────────────────────────┐     ┌─────────────────────┐
│  DANOS-Open (Apache-2.0)│     │  FRR (GPLv2)        │
│  danos-core             │     │  bgpd / ospfd / ... │
│  danos-fib (ZAPI client)│◄───►│  zebra (ZAPI server)│
└─────────────────────────┘     └─────────────────────┘
        ZAPI socket (系统调用例外，不构成衍生作品)
```

### 25.1.4 许可证声明

每个源文件头部包含：
```c
/*
 * Copyright (c) 2026 DANOS-Open Project Contributors.
 * SPDX-License-Identifier: Apache-2.0
 */
```

`LICENSE` 文件为 Apache-2.0 全文。
`NOTICE` 文件列出第三方代码及其许可证。

## 25.2 CLA/DCO 策略

### 25.2.1 DCO（Developer Certificate of Origin）

采用 DCO（轻量级 CLA），贡献者通过 `git commit -s` 签署：

```
Signed-off-by: Jane Doe <jane@example.com>
```

DCO 声明贡献者：
1. 拥有该贡献的版权
2. 有权以 Apache-2.0 许可贡献
3. 理解项目可重新许可或修改

### 25.2.2 企业 CLA（可选）

企业贡献者（员工贡献）需签署企业 CLA，声明：
1. 企业授权员工贡献
2. 企业不主张专利侵权
3. 贡献版权归属项目

CLA 模板参考 LF Networking CLA。

### 25.2.3 CI 自动检查

```yaml
# .github/workflows/dco.yml
- name: DCO check
  uses: tim-action/dco@main
  with:
    check-merge-commit: false
```

PR 缺少 `Signed-off-by` 自动拒绝。

## 25.3 贡献流程

### 25.3.1 RFC 流程

重大架构变更需经过 RFC：

```
1. 提交 RFC 草案（docs/rfcs/000X-title.md）
2. 社区讨论（GitHub Discussions，2 周）
3. RFC 评审委员会评审（2 周）
4. 合并 RFC（标记 Accepted/Rejected/Deferred）
5. 实现 PR 引用 RFC
```

RFC 模板：
```markdown
# RFC-000X: Title

## 动机
## 详细设计
## 替代方案
## 风险
## 兼容性
## 测试计划
```

### 25.3.2 PR 评审流程

```
1. 贡献者提交 PR（含 DCO 签名）
2. CI 自动检查：
   - 编译（x86_64 + aarch64）
   - 单元测试（19+ 用例全绿）
   - 代码风格（clang-format）
   - YANG 模型验证
   - conformance 测试
3. 至少 2 位 Reviewer 批准
4. 至少 1 位 Maintainer 批准（核心模块）
5. 合并（Squash and Merge，保留 DCO）
```

### 25.3.3 CI 门槛

| 检查项 | 要求 | 工具 |
|--------|------|------|
| 编译 | x86_64 + aarch64 通过 | CMake + 交叉编译 |
| 单元测试 | 100% 通过 | ctest |
| 代码覆盖率 | ≥ 80%（核心模块） | gcov + lcov |
| 代码风格 | 0 警告 | clang-format + clang-tidy |
| YANG 验证 | 全模型通过 | yang_validator.py |
| DCO | 所有 commit 签名 | dco-action |
| 安全扫描 | 0 高危 | CodeQL + Trivy |

### 25.3.4 贡献者等级

| 等级 | 权限 | 晋升条件 |
|------|------|---------|
| Contributor | 提交 PR | DCO 签名 |
| Reviewer | 评审 PR | 10+ 合并 PR + 6 个月活跃 |
| Maintainer | 合并 PR + 发布 | 50+ 合并 PR + 1 年活跃 + 2 Maintainer 推荐 |
| Core Maintainer | 架构决策 | Maintainer 选举 + 2/3 多数 |

## 25.4 商标与品牌

### 25.4.1 商标归属

"DANOS-Open" 商标归项目社区所有（或托管基金会）。

使用规则：
1. **项目内使用**：免费，无需授权
2. **衍生产品**：需注明 "based on DANOS-Open"
3. **商业产品**：需商标授权协议（避免品牌混淆）
4. **禁止**：不得暗示官方背书

### 25.4.2 品牌指南

- Logo：项目专用 Logo，不得修改
- 命名：`DANOS-Open`（连字符，首字母大写）
- 域名：`danos-open.org`
- 仓库：`github.com/danos-open/danos-open`

## 25.5 基金会关系

### 25.5.1 托管选项

| 基金会 | 优势 | 劣势 |
|--------|------|------|
| LF Networking | 网络项目生态（ONAP、FD.io） | 治理较重 |
| ONF | SDN/NOS 专注 | 偏控制器 |
| CNCF | K8s 生态集成 | 偏容器 |
| 独立 | 治理灵活 | 缺资源 |

**推荐**：LF Networking（与 FRR、VPP 同生态，FD.io VPP 已托管）。

### 25.5.2 捐赠流程

1. 项目达到 LF Networking 孵化标准（活跃度、多样性、治理）
2. 提交孵化申请
3. LF Networking TOC 评审
4. 孵化期（12 个月）
5. 毕业为正式项目

### 25.5.3 治理文档

- `GOVERNANCE.md`：治理结构、决策流程
- `CONTRIBUTING.md`：贡献指南
- `CODE_OF_CONDUCT.md`：行为准则（Contributor Covenant）
- `SECURITY.md`：安全漏洞披露流程
- `MAINTAINERS.md`：Maintainer 列表与权限

## 25.6 安全披露

### 25.6.1 漏洞披露流程

```
1. 发现者邮件报告 security@danos-open.org（PGP 加密）
2. 安全团队 48h 内确认
3. 安全团队验证 + 修复（90 天 embargo）
4. 发布修复版本 + CVE
5. 公开披露（ embargo 后）
```

### 25.6.2 PGP 密钥

安全团队 PGP 公钥发布在 `SECURITY.md`，用于加密漏洞报告。

## 25.7 MVP 对齐

| 治理项 | MVP 版本 |
|--------|---------|
| Apache-2.0 许可证 | v0.1（已实现） |
| DCO 检查 | v0.1（已实现） |
| CONTRIBUTING.md | v0.1 |
| CODE_OF_CONDUCT.md | v0.1 |
| SECURITY.md | v0.1 |
| RFC 流程 | v0.2 |
| CLA（企业） | v0.3 |
| 基金会托管 | v0.4+ |
| 商标授权 | v0.4+ |
