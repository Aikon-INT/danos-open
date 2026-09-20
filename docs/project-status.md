# DANOS Open Project Status

## Current assessment

As of 2026-09-21, DANOS-Open has moved beyond proof of concept. The project has a runnable management-plane and state-reconciliation core, and is entering the engineering-convergence stage: turning the existing control-plane loop into a repeatable, privileged-environment-verified, deployable NOS baseline.

The strongest capabilities are the DPA object/store/transaction foundation, WAL persistence, desired-to-programmed reconciliation, model-driven gNMI/CLI/NETCONF integration, the initial Linux and VPP backend adapters, observability, and automated protocol/quality tests. The main remaining risk is not the absence of another isolated feature; it is the incomplete proof of the end-to-end loop across FRR, DPA, a real dataplane, restart, deletion, and traffic forwarding.

## Evidence snapshot

- Latest tagged release: `v0.14.0`.
- Current mainline work includes v0.14 release automation, compatibility baselines, event-driven reconciliation and streaming telemetry.
- The configured build registers 34 CTest cases. The restricted development environment initially blocked seven socket/network tests, but the same build passed all 34 cases when re-run with host network privileges. The restriction is therefore environmental, not a current test failure.
- The programming pipeline and composite route tests pass in the current environment.
- K4/K5 was closed in a privileged Docker validation container: K4a/K4b,
  standard gNMI-to-mgrd K5a, and real raw-ICMP forwarding all pass. The
  validation script now handles minimal images without `ping` and no longer
  duplicates veth address configuration.
- VPP protocol interop is implemented and the local VPP protocol suite passes.
  A Debian trixie native-source runtime was built as VPP
  `26.10-rc0~545-gad99177fe`; its API socket, stat socket, and DPA
  conformance (11/11) all pass. DPDK mlx4/mlx5 drivers are intentionally
  disabled for this software/Linux validation image.
- clang/libFuzzer was closed in a Debian Docker toolchain container: clang 19
  built `fuzz_decoders`, and the target ran 10,000 inputs successfully. The
  local host still intentionally has no clang installation.
- The repository now contains the trixie source-build and privileged runtime
  validation assets; no bookworm VPP packages are mixed into trixie.
- Real VPP CLI validation proves a two-path recursive IPv4 ECMP FIB entry with
  a two-bucket load-balance and clean route withdrawal. API-driven forwarding
  remains blocked because the current DANOS binary-API socket framing/
  handshake is rejected by VPP 26.10.

## Capability maturity

| Area | Current maturity | Main gap |
|---|---|---|
| DPA/core state and transactions | Foundation complete | Full rollback/verify contract |
| WAL and restart recovery | Working baseline | Real-disk fsync and migration policy |
| Desired/programmed reconciliation | Working baseline | Complete dependency deletion semantics |
| gNMI/CLI/NETCONF | Strong prototype | Multi-stream edge cases, in-process TLS, long-term protocol maintenance |
| Linux backend | Real backend verified | Broader topology and recovery acceptance |
| VPP backend | Runtime/conformance and interface-address baseline verified | Real forwarding acceptance |
| FRR integration | FIB/ZAPI foundation | BFD translation and multi-protocol topology proof |
| Data model | Route/VRF/NH plus primary interface IPv4/IPv6 model | VLAN, multi-address, tunnel/EVPN models |
| Observability/security | Initial implementation | Operational semantics, HA and upgrade evidence |
| OVS/P4/platform | Intentionally not started | Defer until backend contract is frozen |

## Direction and principles

1. Stabilize and verify the existing control-plane loop before broad protocol expansion.
2. Treat real privileged integration and traffic tests as release evidence, not optional demonstrations.
3. Keep the project value in DPA, state, transactions, reconciliation and backend semantics; reuse mature FRR protocols.
4. Prefer EVPN/SR over legacy VPLS/RSVP/ICCP, but enter EVPN only after tunnel, interface and L3 foundations are complete.
5. Make every feature traceable to code, test, documentation and a Definition of Done.

## Delivery roadmap

### v0.15 Engineering convergence

- Synchronize version/status documentation.
- Land and test the pending gNMI fixes as separate changes.
- Complete Route/NH/NHGroup dependency-aware deletion and retry behavior.
- Complete VPP interface-address messages and multi-address modeling.
- Keep the privileged CI lane green and classify unavailable capabilities as
  environment-blocked rather than code failures.
- Add formal fuzzing targets where the toolchain permits. The `DANOS_TSAN`
  option, libFuzzer target, CI jobs and gNMI concurrent validation are now
  present; this workspace lacks clang, so libFuzzer execution remains a CI
  verification item.
- Establish VPP runtime compatibility baselines. The WAL benchmark now accepts
  `DANOS_WAL_BENCH_PATH` and has a `/var/tmp` CI job for a real-filesystem run.
- Verify the OCI image through boot, configuration, restart and metrics smoke tests.

### v0.16 Minimal L3 NOS loop

Prove interface address, VRF, static route, next-hop, ECMP, BGP/OSPF route installation and withdrawal through gNMI/CLI, DPA, Linux/VPP and real traffic, including restart recovery.

Execution status:

- Interface primary IPv4/IPv6 model and Linux address programming: implemented
  and covered by model, pipeline and regression tests.
- VRF and IPv4 static route/NH/NHGroup pipeline: existing baseline covered by
  deterministic tests.
- ECMP northbound representation: implemented for gNMI `gateways[]`, DPA
  NHGroup creation/update/delete and mock Linux programming; real dataplane
  multipath forwarding remains to be verified.
- Real Linux namespace and traffic proof: blocked in this workspace because
  root/user namespaces are unavailable.
- VPP runtime boot, API/stat sockets and DPA conformance: verified in Debian
  trixie source-built runtime. Interface-address add/delete messages now have
  typed wire coverage and dual-stack adapter programming; real forwarding
  remains pending until binary-API socket compatibility is fixed. CLI-based
  ECMP FIB installation and withdrawal is verified separately.
- FRR BGP/OSPF route installation and withdrawal through the full DPA/backend
  path: remains the next integration milestone; the ZAPI mapper now normalizes
  multipath route messages into multi-member DPA NHGroups and has a regression
  test.

### v0.17 Backend semantic consistency

Freeze capability discovery, idempotency, error, retry, partial-programming and rollback contracts, then apply the same DPA conformance suite to Linux, VPP and a subsequent OVS backend.

### Later

Add VLAN/QinQ, BFD translation, VXLAN and EVPN, followed by HA, upgrade/rollback, scale and only then P4/legacy compatibility work.

## Acceptance policy

Every stage must report four separate results:

- Local deterministic tests.
- Sanitizer/concurrency tests.
- Privileged integration tests with FRR, namespaces or VPP.
- External interoperability and real traffic tests.

An environment restriction must be recorded as `blocked by environment`; it must not be silently counted as pass or fail. A release cannot claim a real dataplane feature until the corresponding privileged and traffic evidence exists.
