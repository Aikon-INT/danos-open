# v1.1 P1 改进动作（9-16）

> v1.1 评审 §九 P1 清单，v0.2 前完成。本目录收录 8 项增强文档，
> 每项与现有代码实现对齐。

## 文档索引

| # | 改进动作 | 文档 | 对应评审章节 |
|---|---------|------|-------------|
| 9 | FRR ZAPI → DPA 映射表 | `appendix_b_zapi_mapping.md` | §4.2 |
| 10 | VPP Backend 实现路径 | `vpp_backend_implementation.md` | §4.3 |
| 11 | 事务模型增强 | `transaction_model_enhanced.md` | §4.4 |
| 12 | Reconciliation 算法增强 | `reconciler_algorithm_enhanced.md` | §4.5 |
| 13 | 对象模型展开 | `object_model_expanded.md` | §5.2 |
| 14 | 标准对齐表（RFC） | `appendix_d_rfc_alignment.md` | §5.3 |
| 15 | 依赖版本兼容矩阵 | `appendix_c_dependency_matrix.md` | §4.6 |
| 16 | 生命周期与升级章节 | `lifecycle_upgrade.md` | §2.1 第 23 章 |

## 完成状态

**8/8 文档完成**（2026-09-06）。每项文档：
- 给出形式化定义/规格
- 与现有代码位置对齐验证
- 标注 v0.1 已实现 vs v0.2+ 规划
- 包含测试覆盖说明

## 与主规格文档的关系

本目录文档增强主规格 `v1.1/DANOS-Open_Architecture_Specification_v1.1.md`
的附录 B/C/D 和第 23 章，提供可指导实施的细节深度。主规格附录保持骨架，
详细内容指向本目录。
