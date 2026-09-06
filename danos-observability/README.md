# danos-observability — 可观测性与 Telemetry

> v0.2+ 计划模块。telemetry / prometheus / tracing / audit。
> 设计依据：`v1.1/observability/observability_telemetry.md`（第 22 章）。

## 职责

- **gNMI Subscribe**：STREAM / ON_CHANGE / SAMPLE 三种模式
- **OpenConfig telemetry paths**：标准化遥测路径
- **Prometheus exporter**：metrics 暴露 + Grafana dashboard 模板
- **流式 telemetry**：gRPC stream、IPFIX/NetFlow
- **结构化日志**：JSON 格式、集中收集（Loki/ELK）
- **分布式追踪**：OpenTelemetry，跨 FRR → Adapter → DPA → Backend
- **告警**：Alertmanager 规则、Runbook
- **审计**：Transaction ID 关联操作主体、时间、变更 diff

## 状态

骨架目录（v1.1 §3.1 仓库结构对齐）。设计文档已完成，实现待 v0.2+。

## 接口边界

- 与 `danos-core`：订阅事件总线、Transaction 生命周期
- 与 `danos-mgmt/gnmi`：gNMI Subscribe 服务端实现
- 与 `danos-security`：审计日志接入
