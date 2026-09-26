# DANOS-Open

> Software-First, ASIC-SDK-Free Disaggregated Network Operating System.
>
> Built with Linux, FRR, VPP, DPDK, and P4 — unified by the DANOS Data Plane
> Abstraction (DPA).

## Current v0.16 status

`v0.16.0-rc1` is the current release candidate. The scoped L3 NOS software
closure is covered by CTest 34/34, topology-89 FRR/VPP lifecycle recovery,
QEMU e1000 two-port forwarding, and VMware VMXNET3 polling-only packet
baselines (about 100 pps, 0% loss). These virtualized results are functional
and regression evidence, not line-rate DPDK performance. Real PCI DPDK remains
`ENVIRONMENT-OPEN` until a dedicated VFIO/uio, HugePages, VPP-DPDK runner is
available. BFD, VLAN, VXLAN, and EVPN remain deferred until the formal
`v0.16.0` release.

Authoritative status: [v0.16 acceptance matrix](docs/v0.16-acceptance-matrix.md)
and [project status](docs/project-status.md). The release-candidate gate is
`bash danos-test/integration/run_v016_release_gate.sh`.

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

Expected output: 34 ctest suites pass, including the v0.3 vertical
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

## Running the System

```bash
cmake -B build && cmake --build build -j$(nproc)
./build/danos-mgrd/danos-mgrd --seed --port 59200 --metrics-port 59201
# another shell:
gnmic -a 127.0.0.1:59200 --insecure get --path /interfaces
curl http://127.0.0.1:59201/metrics
# kill -9 and restart — config persists (WAL replay)
```

A systemd unit ships in `danos-mgrd/danos-mgrd.service`.

## Verified Interoperability

Driven with [gnmic](https://github.com/openconfig/gnmic) v0.35
(independent grpc-go client) against our hand-written C
protobuf/HPACK/HTTP2/gRPC stack:

| Check | Result |
|-------|--------|
| Capabilities / Get / Set / Subscribe (ONCE + STREAM push) | pass |
| HPACK Huffman headers, flow control (79KB > default window), 8 concurrent streams | pass |
| TLS channel (socat front-end) | pass |
| Model-driven leaf paths (openconfig /config, /state) | pass |
| 32 concurrent connections (scale test, plain + ASAN) | pass |

Reproduce: `bash danos-test/integration/run_v0.4_interop.sh`
(see `docs/interop/v0.4_interop_dod.md`).

Release acceptance: `bash danos-test/integration/release_check.sh`
(ctest + gnmic interop + mgrd boot/crash-recovery smoke).

Quality gates in CI: full ctest, ASAN+UBSAN (incl. decoder fuzzing),
gnmic interop, 32-connection scale test — see
`.github/workflows/{ci,interop}.yml` and `docs/threading.md`.

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

Documentation map: `docs/README.md` (single doc root; current assessment and
roadmap in `docs/project-status.md`; v1.1 spec archived at `docs/archive/v1.1/`).

## Repository Structure

| Module | Description | Status |
|--------|-------------|-------|
| `danos-core/` | Core engine: object/store/transaction/capability/event, WAL persist, dependency-aware programming pipeline, reconciler | v0.10 |
| `danos-dpa/` | DPA public API (C ABI + Protobuf) | v0.2 |
| `danos-fib/` | FRR zebra/FIB adapter | v0.2 |
| `danos-vpp/` | VPP backend: real binary API (handshake + msg table + typed messages) + stat segment + programming adapter (v0.11) | v0.11 |
| `danos-models/` | YANG / OpenConfig models (13 models; interfaces subtree bound to gNMI via model_paths) | v0.5 |
| `danos-mgmt/` | CLI / gNMI (real gRPC, model-driven, streaming Subscribe) / NETCONF (model-wired edit-config) — all three share the model layer | v0.7 |
| `danos-security/` | RBAC / audit log; CoPP policer programming (vpp_msgs) | v0.2 (CoPP v0.4) |
| `danos-ha/` | BFD session manager (protocol delegated to FRR bfdd per ADR-0006; translation layer pending) / VRRP skeleton / supervisor | v0.2 |
| `danos-observability/` | Prometheus exposition (real HTTP server, stat-provider bridge) / structured logging | v0.6 |
| (core) `persist` | WAL-backed durable config: boot replay + torn-record tolerance | v0.3 |
| (mgmt) `model_paths` | YANG path registry: leaf-level gNMI Get/Set, gRPC NotFound/InvalidArgument errors | v0.5 |
| (mgmt) `model_routes` | composite route write: one gNMI update -> NH+Group+Route, cascade delete | v0.12 |
| `danos-mgrd` | system daemon: WAL boot + gNMI + Prometheus /metrics + crash recovery | v0.6 |
| `danos-netlink` | kernel backend adapter: rtnetlink (real) / in-memory FIB (mock), drives the ADR-0007 programming pipeline | v0.9 |
| (core) `programming` | desired→backend pipeline with PROGRAMMED ledger, tombstone sweep | v0.10 |
| `danos-compat/` | OcNOS-like CLI translation (minimal; awaiting model-layer rebasing) | v0.3 |
| `danos-platform/` | Platform adaptation: x86 / ARM / generic | not started (intentional) |
| `danos-ovs/` | OVS-DPDK backend | not started (intentional) |
| `danos-p4/` | P4Runtime / P4 backend | not started (intentional) |
| `danos-test/` | conformance / interop / fuzz / scale / perf / kernel tests (33 suites total) | v0.10 |
| `danos-build/` | Toolchain image + minimal mgrd deployment image (`Dockerfile.mgrd`) | v0.6 |
| `docs/` | Single documentation root: ADRs, interop DoD, compat matrix, threading contract, release notes | rolling |

Module charters (design-era): see `docs/archive/v1.1/repo-structure/`.

## License

Apache-2.0. See [LICENSE](LICENSE).

FRR (GPLv2) runs as a separate process; DANOS-Open communicates via ZAPI
socket and does not link FRR libraries.
