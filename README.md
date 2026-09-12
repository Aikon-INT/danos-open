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

Expected output: 28 ctest suites pass, including the v0.3 vertical
stack suites:
```
vpp_proto     — VPP binary API handshake + typed messages + stat segment (mock VPP server)
gnmi_proto    — gNMI protobuf wire format (exact-byte tests)
hpack         — HPACK RFC 7541 (Appendix C.6.1 known-answer vector)
gnmi_grpc     — real gRPC over TCP: Capabilities/Get/Set vs DPA store
persist       — WAL restart cycle: log -> replay -> reconcile
```

### End-to-end verification

```bash
bash danos-test/integration/run_v0.3_verify.sh --with-frr
```

Runs V1-V4 (VPP protocol conformance, gNMI gRPC roundtrip, persistence
restart cycle, gNMI Set surviving a restart) and V5 (real FRR zebra
reachability via the danos-frr-test image).

## Verified Interoperability

Driven with [gnmic](https://github.com/openconfig/gnmic) v0.35
(independent grpc-go client) against our hand-written C
protobuf/HPACK/HTTP2/gRPC stack:

| Check | Result |
|-------|--------|
| Capabilities / Get / Set / Subscribe (ONCE + STREAM push) | pass |
| HPACK Huffman headers, flow control (79KB > default window), 8 concurrent streams | pass |
| TLS channel (socat front-end) | pass |

Reproduce: `bash danos-test/integration/run_v0.4_interop.sh`
(see `danos-docs/interop/v0.4_interop_dod.md`).

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
| `danos-core/` | Core engine: object/state/transaction/capability/event/reconciler | v0.2 |
| `danos-dpa/` | DPA public API (C ABI + Protobuf) | v0.2 |
| `danos-fib/` | FRR zebra/FIB adapter | v0.2 |
| `danos-vpp/` | VPP backend: real binary API (handshake + msg table + typed messages) + stat segment | v0.3 |
| `danos-models/` | YANG / OpenConfig models (13 models) | v0.2 |
| `danos-mgmt/` | CLI / gNMI (real protobuf + gRPC/HTTP2) / NETCONF | v0.3 |
| `danos-security/` | RBAC / CoPP / audit log | v0.2 |
| `danos-ha/` | BFD multihop / VRRP / supervisor | v0.2 |
| `danos-observability/` | Prometheus / structured logging / alerts | v0.2 |
| (core) `persist` | WAL-backed durable config: boot replay + torn-record tolerance | v0.3 |
| (mgmt) `model_paths` | YANG path registry: leaf-level gNMI Get/Set, gRPC NotFound/InvalidArgument errors | v0.5 |
| `danos-compat/` | OcNOS-like CLI & semantic compatibility layer | v0.3+ |
| `danos-platform/` | Platform adaptation: x86 / ARM / generic | v0.3+ |
| `danos-ovs/` | OVS-DPDK backend | v0.3+ |
| `danos-p4/` | P4Runtime / P4 backend | v0.4+ |
| `danos-test/` | Unit / integration / conformance / topology / perf tests | v0.2 |
| `danos-build/` | Debian / Ubuntu / container / OCI image | v0.1 |
| `danos-docs/` | Architecture / RFC / API spec / ADR / runbook | v0.1 |

See `v1.1/repo-structure/repository_structure_v1.1.md` for full module charter.

## License

Apache-2.0. See [LICENSE](LICENSE).

FRR (GPLv2) runs as a separate process; DANOS-Open communicates via ZAPI
socket and does not link FRR libraries.
