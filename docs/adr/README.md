# Architecture Decision Records (ADR)

This directory records architecture decisions for DANOS-Open.

## Index

| ADR | Title | Status | Date |
|-----|-------|--------|------|
| 0001 | Use DPA as Data Plane Abstraction (not SAI) | Accepted | 2024-12-01 |
| 0002 | VPP as Primary Dataplane Backend | Accepted | 2024-12-01 |
| 0003 | FRR as Control Plane via Process Isolation | Accepted | 2024-12-01 |
| 0004 | Apache-2.0 License for Core, GPLv2 for FRR | Accepted | 2024-12-01 |
| 0005 | Transaction-Based Configuration Model | Accepted | 2024-12-01 |
| 0006 | BFD 会话由 FRR bfdd 承载,DANOS-Open 只做状态聚合 | Accepted | 2026-09-13 |
| 0007 | Backend Adapter 管线与 PROGRAMMED 生命周期 | Accepted | 2026-09-13 |

## Format

Each ADR follows:
- **Title**: Short noun phrase
- **Context**: Why this decision was needed
- **Decision**: What was decided
- **Consequences**: Positive/negative implications
- **Alternatives**: What was considered and rejected
