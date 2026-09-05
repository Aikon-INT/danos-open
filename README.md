# DANOS-Open

> Software-First, ASIC-SDK-Free Disaggregated Network Operating System.
>
> Built with Linux, FRR, VPP, DPDK, and P4 — unified by the DANOS Data Plane
> Abstraction (DPA).

## Quick Start (5 minutes)

### Prerequisites

- Linux 6.6+ (LTS)
- GCC 13+
- CMake 3.16+

### Build

```bash
git clone https://github.com/danos-open/danos-open.git
cd danos-open
cmake -B build
cmake --build build -j$(nproc)
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

### Verify DPA API

```bash
./build/danos-test/conformance_test
```

Expected output:
```
=== DPA Conformance Suite: 11 tests ===
[RUN ] tx_lifecycle ... PASS
...
=== Result: 11 passed, 0 failed, 11 total ===
```

## Architecture

```
Management (CLI/gNMI/NETCONF)
        ↓
   DANOS DPA (Capability + Transaction + Reconciliation)
        ↓
   VPP / OVS-DPDK / P4-DPDK
        ↓
   Linux / DPDK / NIC
```

See `v1.1/DANOS-Open_Architecture_Specification_v1.1.md` for full spec.

## Repository Structure

| Module | Description |
|--------|-------------|
| `danos-core/` | Core engine: object/state/transaction/capability/event/reconciler |
| `danos-dpa/` | DPA public API (C ABI + Protobuf) |
| `danos-fib/` | FRR zebra/FIB adapter |
| `danos-vpp/` | VPP backend |
| `danos-models/` | YANG models |
| `danos-mgmt/` | CLI / gNMI / NETCONF |
| `danos-test/` | Unit / integration / conformance / topology tests |

## License

Apache-2.0. See [LICENSE](LICENSE).

FRR (GPLv2) runs as a separate process; DANOS-Open communicates via ZAPI
socket and does not link FRR libraries.
