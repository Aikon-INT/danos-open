# DANOS Open Project Status

## Current assessment

As of 2026-09-21, DANOS-Open has moved beyond proof of concept. The project has a runnable management-plane and state-reconciliation core, and is entering the engineering-convergence stage: turning the existing control-plane loop into a repeatable, privileged-environment-verified, deployable NOS baseline.

The strongest capabilities are the DPA object/store/transaction foundation, WAL persistence, desired-to-programmed reconciliation, model-driven gNMI/CLI/NETCONF integration, the Linux and VPP backend adapters, observability, and automated protocol/quality tests. The project is now in integration closure: the main remaining risk is the incomplete proof of the end-to-end loop across FRR, DPA, a real dataplane, restart, deletion, and traffic forwarding.

The current mainline is clean and synchronized with `origin/main` at `869dd9b`; the complete deterministic suite passes 34/34. This is strong software evidence, but it is not a substitute for the privileged FRR-to-VPP route lifecycle evidence below.

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
- Real VPP CLI and API validation prove a two-path IPv4 ECMP FIB lifecycle:
  API handshake, control ping, interface flags, route add/delete, stat segment,
  and CLI two-bucket load-balance inspection all pass against VPP 26.10.
- Packet-level VPP forwarding is verified in a privileged trixie container:
  two Linux namespaces crossed two VPP af_packet interfaces with bidirectional
  ICMP success (the initial ARP-learning packet may be lost).
- FRR trixie topology baseline is verified: BGP EVPN reaches Established and
  OSPF reaches Full/2-Way; the existing OSPF/LDP acceptance also confirms
  ldpd is running and configured. Full FRR ZAPI → DPA → VPP route lifecycle
  remains the integration gap.
- The 3-node FRR BGP/ECMP and OSPF convergence scripts pass with ping
  reachability. WAL/DPA restart recovery V3/V4 and the 1000-object transaction
  scale test also pass. The privileged software forwarding baseline records
  0.4363 Mpps; it is below the VPP+DPDK target and is not treated as a VPP
  dataplane failure.
- Real FRR zebra ZAPI reachability is verified in the trixie runtime and the
  parser/mapper/e2e mock gates pass. `fib_live_bridge` now implements the
  FRR v6 HELLO/session, replay requests, native route decoding, redistribute
  route notification commands 31/32, DPA transaction wiring and VPP adapter
  entry point. A real static route can be present in the FRR RIB, but the
  current temporary topology has not yet produced a valid redistribute event;
  live route install/withdraw therefore remains open.
- The current privileged probe enables zebra packet logging. It proves that
  FRR receives the static route and emits a redistribute route event to other
  subscribed clients, while the DANOS registration socket is logged as
  receiving `unknown command 11` in the active runtime. This contradicts the
  FRR 10.3 source handler table, which contains `ZEBRA_REDISTRIBUTE_ADD=11`;
  the next check is socket inode/process ownership and zebra startup/runtime
  identity, not another unverified business-payload change.
- The VPP image contains `dpdk_plugin.so`, but the active validation runtime
  does not load DPDK and exposes no PCI dataplane device. The VPP+DPDK lane is
  therefore environment-blocked, not a passed performance result.

## Capability maturity

| Area | Current maturity | Main gap |
|---|---|---|
| DPA/core state and transactions | Foundation complete | Full rollback/verify contract |
| WAL and restart recovery | Working baseline | Real-disk fsync and migration policy |
| Desired/programmed reconciliation | Working baseline | Complete dependency deletion semantics |
| gNMI/CLI/NETCONF | Strong prototype | Multi-stream edge cases, in-process TLS, long-term protocol maintenance |
| Linux backend | Real backend verified | Broader topology and recovery acceptance |
| VPP backend | Runtime/conformance/API ECMP and packet-forwarding verified | Traffic scale and recovery |
| FRR integration | Live bridge entry point plus ZAPI/BGP/OSPF baseline | Privileged route lifecycle, daemon hardening and BFD |
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
  typed wire coverage and dual-stack adapter programming. The API client now
  reaches the live VPP route transaction, but the current software runtime
  rejects the FRR gateway route with `retval=-54` because it exposes only
  `local0` and no usable dataplane interface/path. Real API-driven route,
  ECMP and packet forwarding remain open for the dedicated topology.
- FRR BGP/OSPF route installation and withdrawal through the full DPA/backend
  path: remains the next integration milestone; the ZAPI mapper now normalizes
  multipath route messages into multi-member DPA NHGroups and has a regression
  test.
- The runnable `build/danos-test/fib_live_bridge` now connects a real FRR zebra
  socket, dispatches each message through the FIB mapper and DPA transaction,
  and drives the VPP adapter. Use `--messages N` for deterministic acceptance.
  The DPDK lane preflight is `danos-test/integration/run_vpp_dpdk_lane.sh`; it
  reports SKIP when no PCI/VFIO device is exposed and never treats a kernel or
  mock dataplane as DPDK evidence.

## Next-stage implementation plan

The project is now in integration closure rather than broad feature expansion.
The recommended order is:

1. Replace ad-hoc containers with a fixed Debian trixie FRR+VPP compose/test
   topology. Explicitly share `/run/vpp` and `/var/run/frr`, add readiness
   checks, preserve logs on failure, and keep zebra/VPP/bridge alive together.
2. The FRR 10.3 registration and route-notification path is now proven: the
   live bridge receives command 31 route events and enters the DPA transaction.
   Next, provide a VPP interface/path, then prove a single static add/withdraw
   first, followed by BGP/OSPF and ECMP. A replay message is not counted as
   route redistribution evidence.
3. Verify each route event through DPA and `vppctl show ip fib`, then run real
   traffic, withdraw, reconnect and restart recovery. The bridge now invokes
   the programming sweep after each committed ZAPI transaction so deleted
   routes are withdrawn from VPP using the programmed ledger copy.
4. Close Route/NH/NHGroup dependency deletion, tombstone, retry and rollback
   semantics, then repeat the lifecycle on Linux and VPP backends.
5. Run the dedicated DPDK lane only on a host exposing PCI/VFIO, hugepages and
   a loaded VPP DPDK plugin. The current software-forwarding result is a
   baseline, not DPDK evidence.

VMXNET3 is a valid option for this lane only when the test runner itself is a
VMware guest with a VMXNET3 PCI NIC (vendor/device `15ad:07b0`). It cannot be
created by mounting a driver or socket into a container. The lane preflight now
requires a real PCI Ethernet function and reports the VMXNET3 device when
present; the guest NIC must then be made available through the selected DPDK
binding/VFIO setup.

The repeatable socket-level driver is
`danos-test/integration/run_frr_zapi_vpp.sh`. It classifies missing FRR/VPP
sockets as `BLOCKED`, invokes the same `fib_live_bridge` binary used by the
integration build, and reports success only after the bridge has processed the
requested message count. The caller must supplement this with `vppctl` FIB
inspection and packet traffic evidence.

The v0.16 Definition of Done is a reproducible FRR BGP/OSPF route
installation and withdrawal trace from ZAPI through DPA to the real VPP FIB,
including traffic, restart and deletion evidence. OVS/P4, broad model growth
and additional protocol work remain deferred until this loop is stable.

The bridge sends the FRR v6 `ZEBRA_HELLO` registration using the official
10-byte zserv header, and `zapi_parse_frr()` has a regression-tested parser for
that header. The FRR-native route payload decoder is connected to the live
session; redistribute route notifications use the distinct FRR v6 commands
  31/32 rather than the client-originated route commands 9/10. The live session
requests router-id/interface replay and IPv4/IPv6 connected, static, OSPF and
BGP notifications. The remaining gap is a usable VPP dataplane path and
privileged route lifecycle evidence: real add/withdraw, ECMP, VPP FIB
inspection and traffic. The current socket-connected runtime receives the FRR
route and enters DPA. The adapter accepts an explicit
`DANOS_VPP_IFINDEX_MAP=linux_ifindex:vpp_sw_if_index,...` mapping for
namespace-local interface IDs; with `2:1` in the current FRR/VPP host-
interface topology, the same route reaches VPP and is installed. The mapping
is a topology requirement, not a ZAPI registration failure.
The Debian trixie FRR 10.3 runtime uses `/var/run/frr/zserv.api` as the
default zserv socket; the session and acceptance harness now use that path,
while explicit socket overrides remain supported.

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
