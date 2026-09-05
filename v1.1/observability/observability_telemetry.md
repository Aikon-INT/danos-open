# 第 22 章 可观测性与 Telemetry

> v1.1 新增章节。可观测性是生产级 NOS 的必备能力。

## 22.1 可观测性三大支柱

| 支柱 | 实现 | 章节 |
|------|------|------|
| Metrics | Prometheus exporter + Alertmanager | §22.2 |
| Logs | 结构化 JSON + 集中收集 | §22.3 |
| Traces | OpenTelemetry 分布式追踪 | §22.4 |

补充：
| 流式数据 | gNMI Subscribe + IPFIX | §22.5 |
| 审计 | Transaction 审计链 | §22.6（见 §20.9） |

## 22.2 Metrics

### 22.2.1 Prometheus Exporter

- 端口：`/metrics`（默认 9191）
- 采集频率：15s（Prometheus scrape）
- 标签规范：`danos_{object}_{metric}{labels}`

### 22.2.2 核心 Metric 清单

**系统级**：
| Metric | 类型 | 说明 |
|--------|------|------|
| `danos_uptime_seconds` | gauge | 进程运行时间 |
| `danos_version_info` | gauge(1) | 版本信息（标签） |
| `danos_backend_up` | gauge | backend 连接状态 |

**事务级**：
| Metric | 类型 | 说明 |
|--------|------|------|
| `danos_tx_total` | counter | 事务总数 |
| `danos_tx_duration_seconds` | histogram | 事务耗时分布 |
| `danos_tx_failed_total` | counter | 失败事务数（按错误码标签） |
| `danos_tx_active` | gauge | 活跃事务数 |

**对象级**：
| Metric | 类型 | 说明 |
|--------|------|------|
| `danos_objects_total` | gauge | 各对象数量（按类型标签） |
| `danos_route_total` | gauge | 路由数（按 vrf、protocol 标签） |
| `danos_acl_rules_total` | gauge | ACL 规则数 |

**Reconcile 级**：
| Metric | 类型 | 说明 |
|--------|------|------|
| `danos_reconcile_runs_total` | counter | reconcile 次数 |
| `danos_reconcile_diffs_total` | counter | 发现 diff 次数 |
| `danos_reconcile_repairs_total` | counter | 修复次数 |
| `danos_reconcile_failures_total` | counter | 修复失败次数 |
| `danos_reconcile_flaps_total` | counter | 震荡次数 |

**协议级**（来自 FRR）：
| Metric | 类型 | 说明 |
|--------|------|------|
| `danos_bgp_peers_up` | gauge | BGP peer up 数 |
| `danos_bgp_routes_received` | gauge | 接收路由数（按 peer） |
| `danos_ospf_neighbors_full` | gauge | OSPF neighbor full 数 |

**性能级**（来自 VPP）：
| Metric | 类型 | 说明 |
|--------|------|------|
| `danos_iface_rx_packets` | counter | 接收包数 |
| `danos_iface_tx_packets` | counter | 发送包数 |
| `danos_iface_rx_bytes` | counter | 接收字节数 |
| `danos_iface_tx_bytes` | counter | 发送字节数 |
| `danos_iface_rx_drops` | counter | 接收丢包 |
| `danos_iface_tx_drops` | counter | 发送丢包 |

### 22.2.3 Grafana Dashboard

- 预置 dashboard JSON（`danos-observability/dashboards/`）
- 面板：概览 / 路由 / 接口 / BGP / 事务 / Reconcile / 告警

### 22.2.4 告警规则

| 告警 | 条件 | 严重度 |
|------|------|--------|
| BackendDown | `danos_backend_up == 0` for 30s | critical |
| TxFailureRate | `rate(danos_tx_failed_total[5m]) / rate(danos_tx_total[5m]) > 0.1` | warning |
| ReconcileFailure | `rate(danos_reconcile_failures_total[5m]) > 0` | warning |
| ReconcileFlap | `rate(danos_reconcile_flaps_total[5m]) > 0` | critical |
| BgpPeerDown | `danos_bgp_peers_up < expected` for 60s | warning |
| RouteCountDrop | `danos_route_total drops > 10%` in 5m | critical |

## 22.3 日志

### 22.3.1 结构化日志

- 格式：JSON
- 字段：`timestamp, level, module, tx_id, obj_type, obj_id, message, ...`
- 输出：本地文件 + syslog + 可选 gRPC stream

### 22.3.2 日志级别

| 级别 | 用途 | 默认 |
|------|------|------|
| ERROR | 错误（需关注） | 启用 |
| WARN | 警告 | 启用 |
| INFO | 重要操作 | 启用 |
| DEBUG | 调试 | 禁用 |
| TRACE | 详细追踪 | 禁用 |

### 22.3.3 集中收集

- 推荐：Loki + Promtail 或 ELK
- 日志保留：默认 30 天
- 敏感信息脱敏：密钥、密码、证书私钥不出现于日志

## 22.4 分布式追踪

### 22.4.1 OpenTelemetry 集成

- 一次管理面操作（如 gNMI Set）产生一个 trace
- trace 跨越：gNMI → DPA Core → Transaction → Backend → Verify
- 每个阶段一个 span

### 22.4.2 Span 示例

```
Trace: gnmi-set-route-abc123
├── span: gnmi-server (2ms)
├── span: dpa-tx-begin (0.1ms)
├── span: dpa-tx-prepare (5ms)
│   └── span: vpp-api-route-add (4ms)
├── span: dpa-tx-validate (1ms)
├── span: dpa-tx-commit (3ms)
├── span: dpa-tx-verify (2ms)
└── span: dpa-tx-done (0.1ms)
```

### 22.4.3 采样

- 默认采样率：1%（生产）
- 错误事务 100% 采样
- 可按对象类型/操作类型配置采样

## 22.5 流式 Telemetry

### 22.5.1 gNMI Subscribe

- 模式：ON_CHANGE / SAMPLE / STREAM
- 路径：OpenConfig YANG 路径
- 推送：gRPC stream 到 collector

### 22.5.2 IPFIX/NetFlow

- VPP IPFIX exporter
- 流记录：src/dst/protocol/bytes/packets
- 采集到外部 collector（nfcapd、GoFlow2）

## 22.6 审计（见 §20.9）

审计日志是可观测性的合规子集，详见安全架构 §20.9。

## 22.7 可观测性 MVP 对齐

| 特性 | v0.1 | v0.2 | v0.5+ |
|------|------|------|-------|
| Prometheus exporter（基础 metrics） | ✅ | | |
| 结构化 JSON 日志 | ✅ | | |
| gNMI Subscribe（ON_CHANGE） | | ✅ | |
| Grafana dashboard 预置 | | ✅ | |
| OpenTelemetry 追踪 | | | ✅ |
| IPFIX | | | ✅ |
| Alertmanager 规则 | | ✅ | |
| 审计日志完整 | ✅ | | |
