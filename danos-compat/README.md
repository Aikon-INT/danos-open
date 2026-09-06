# danos-compat — OcNOS 兼容层

> v0.2+ 计划模块。OcNOS-like CLI 与语义兼容映射层。

## 职责

- OcNOS 风格 CLI 命令翻译到 DANOS DPA Transaction
- 语义映射：OcNOS 配置语义 → DPA 对象模型
- 配置导入/导出：OcNOS 配置格式 ↔ DPA YANG 模型
- 行为兼容：默认值、校验规则、错误码对齐

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。实现计划见 v1.1 评审 P1 改进动作清单。

## 接口边界

- 输入：OcNOS-like CLI 命令、配置片段
- 输出：DPA Transaction（提交给 `danos-core`）
- 依赖：`danos-core`、`danos-mgmt/cli`
