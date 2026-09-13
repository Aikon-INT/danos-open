# DANOS-Open v0.6.0

> Software-First, ASIC-SDK-Free Disaggregated NOS — first release with a
> runnable system daemon and externally verified management plane.

## Highlights

- **Model-driven gNMI** (real gRPC: protobuf + HPACK/Huffman + flow
  control + streaming Subscribe), externally verified with openconfig
  gnmic v0.35 — capabilities/get/set/subscribe-ONCE/subscribe-STREAM
  push, TLS front-end, 79 KB responses through the default window,
  8 concurrent streams, proper gRPC status semantics (NotFound/
  InvalidArgument/Unknown).
- **danos-mgrd**: the control-plane daemon — WAL boot recovery, gNMI
  serving, Prometheus /metrics, signal-driven shutdown; survives
  `kill -9` with configuration intact.
- **Durable configuration**: WAL with per-record CRC, torn-write
  tolerance, reconciler-driven re-program after recovery.
- **VPP backend** (protocol-conformance verified): sockclnt handshake,
  dynamic message table, typed messages (interfaces, routes, neighbors,
  VRFs, CoPP policer), stat segment via SCM_RIGHTS + mmap.
- **Observability**: Prometheus exposition (139 metrics) with a
  stat-provider bridge to the VPP stat segment.
- **Performance baseline**: 0.001 ms/object per-object transactions,
  0.2 ms batch commit of 1000 objects, 3.1 ms WAL recovery of 1000
  objects (tmpfs; see compat matrix).

## Verification

- 29 ctest suites green; release acceptance
  (`danos-test/integration/release_check.sh`) covers ctest + gnmic
  interop + mgrd crash-recovery smoke.
- Interop DoD I1-I11 complete except V-group (requires a reachable
  VPP; all client-side code and scripts are ready:
  `run_v0.4_interop.sh --with-vpp`).

## Known limitations

- VPP field layouts are frozen against VPP master .api files; version
  probing lands with the first real-VPP interop run.
- Model path registry covers openconfig-interfaces (leaf level) plus
  vrfs/routes list level; hand-written table, generation pipeline
  planned for v0.7.
- NETCONF/CLI front ends are not yet routed through the model registry.
- No TLS in-process (socat/stunnel front-end validated instead).

## Checksums & build

Build: `cmake -B build && cmake --build build -j$(nproc)`
Accept: `bash danos-test/integration/release_check.sh`
