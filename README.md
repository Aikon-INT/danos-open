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

| Module | Description | Status |
|--------|-------------|-------|
| `danos-core/` | Core engine: object/state/transaction/capability/event/reconciler | v0.1 |
| `danos-dpa/` | DPA public API (C ABI + Protobuf) | v0.1 |
| `danos-fib/` | FRR zebra/FIB adapter | v0.1 |
| `danos-vpp/` | VPP backend | v0.1 |
| `danos-models/` | YANG / OpenConfig models | v0.1 |
| `danos-mgmt/` | CLI / gNMI / NETCONF | v0.1 |
| `danos-compat/` | OcNOS-like CLI & semantic compatibility layer | v0.2+ |
| `danos-ha/` | NSR / VRRP / EVPN-MH / supervisor / ISSU | v0.2+ |
| `danos-observability/` | Telemetry / Prometheus / tracing / audit | v0.2+ |
| `danos-security/` | AAA / TLS / key management / CoPP | v0.2+ |
| `danos-platform/` | Platform adaptation: x86 / ARM / generic | v0.2+ |
| `danos-ovs/` | OVS-DPDK backend | v0.3+ |
| `danos-p4/` | P4Runtime / P4 backend | v0.4+ |
| `danos-test/` | Unit / integration / conformance / topology / perf tests | v0.1 |
| `danos-build/` | Debian / Ubuntu / container / OCI image | v0.1 |
| `danos-docs/` | Architecture / RFC / API spec / ADR / runbook | v0.1 |

See `v1.1/repo-structure/repository_structure_v1.1.md` for full module charter.

## License

Apache-2.0. See [LICENSE](LICENSE).

FRR (GPLv2) runs as a separate process; DANOS-Open communicates via ZAPI
socket and does not link FRR libraries.
