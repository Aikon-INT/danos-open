# DANOS-Open Dependency Version Matrix

> G5: 依赖版本锁定。所有构建必须使用以下版本组合。

## v0.1 兼容矩阵

| 依赖 | 版本 | 备注 |
|------|------|------|
| FRR | 10.2.x | 通过 ZAPI socket 通信，不链接库 |
| VPP | 24.06.x | Primary dataplane |
| DPDK | 23.11.x (LTS) | VPP 依赖 |
| OVS | - | v0.3 引入 |
| P4Runtime | - | v0.4 引入 |
| Linux | 6.6+ (LTS) | VFIO+IOMMU |
| GCC | 13+ | C11 |
| CMake | 3.16+ | |

## 版本锁定文件

构建时使用 `danos-build/deps/versions.lock` 锁定确切版本：

```ini
# danos-build/deps/versions.lock
FRR_VERSION=10.2.1
VPP_VERSION=24.06.0
DPDK_VERSION=23.11.1
LINUX_MIN_VERSION=6.6
GCC_MIN_VERSION=13
CMAKE_MIN_VERSION=3.16
```

## 兼容性规则

1. **major** 版本变更需要 DANOS-Open major 升级
2. **minor** 版本变更在 CI 矩阵测试通过后接受
3. **patch** 版本变更自动接受（bug fix）
4. 任何依赖升级必须通过全量 CI + conformance 测试
