# DANOS Open Project Status

## Current assessment

As of 2026-09-21, DANOS-Open has moved beyond proof of concept. The project has a runnable management-plane and state-reconciliation core, and is entering the engineering-convergence stage: turning the existing control-plane loop into a repeatable, privileged-environment-verified, deployable NOS baseline.

The strongest capabilities are the DPA object/store/transaction foundation, WAL persistence, desired-to-programmed reconciliation, model-driven gNMI/CLI/NETCONF integration, the Linux and VPP backend adapters, observability, and automated protocol/quality tests. The FRR→DPA→VPP route loop is now verified for add, withdraw, weighted ECMP, traffic and desired-state replay; the remaining external dependency is a real VFIO-bound DPDK runner.

The current mainline is clean and synchronized with `origin/main`; the complete deterministic suite passes 34/34. Privileged evidence is recorded separately from the DPDK hardware gate.

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
  ldpd is running and configured. The live FRR ZAPI → DPA → VPP route
  lifecycle is verified in the privileged trixie topology below.
- The 3-node FRR BGP/ECMP and OSPF convergence scripts pass with ping
  reachability. WAL/DPA restart recovery V3/V4 and the 1000-object transaction
  scale test also pass. The privileged software forwarding baseline records
  0.4363 Mpps; it is below the VPP+DPDK target and is not treated as a VPP
  dataplane failure.
- Real FRR zebra ZAPI reachability, FRR 10.3 registration, route commands
  31/32, native weighted multipath decoding and DPA/VPP programming are
  verified. A real static add/delete, two resolved ECMP paths, VPP ping
  5/5 with 0% loss, and programmed-ledger withdrawal all pass. The bridge
  also replays desired routes after an idle VPP restart probe.
- The VPP image contains `dpdk_plugin.so`, but the active validation runtime
  does not load DPDK and exposes no PCI dataplane device. The VPP+DPDK lane is
  therefore environment-blocked, not a passed performance result.
- 2026-09-22 QEMU/Debian-trixie guest validation closed the software DPDK
  lane: Debian kernel `uio.ko` and `uio_pci_generic.ko` are bundled, two QEMU
  e1000 devices are bound by VPP DPDK, API/stats sockets are ready, both
  interfaces are up, and a two-path `30.30.30.0/24` load-balance FIB is
  installed and reproduced after two guest restarts. QEMU VMXNET3 (`15ad:07b0`)
  is enumerated and UIO-bound, but its interface-control path still triggers
  QEMU VMXNET3 `cafe000f`/VPP instability; it is not counted as passed traffic.
- FRR 10.3 standard `frrinit.sh` startup (`watchfrr + zebra + mgmtd +
  staticd`) was reproduced in a privileged container. The live bridge now
  subscribes to `ZEBRA_ROUTE_STATIC=3` by default and accepts native route
  notifications (9/10) as well as FRR redistribute notifications (31/32).
  A static route replay reached the bridge (`processed=1`); VPP programming
  still requires a matching live-interface ifindex map in the same topology.

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
  `local0` and no usable dataplane interface/path. The dedicated software
  topology now proves API-driven route, ECMP and packet forwarding; only the
  separate DPDK hardware lane remains open.
- FRR BGP/OSPF route installation and withdrawal through the full DPA/backend
  path: remains the next integration milestone; the ZAPI mapper now normalizes
  multipath route messages into multi-member DPA NHGroups and has a regression
  test.
- The runnable `build/danos-test/fib_live_bridge` now connects a real FRR zebra
  socket, dispatches each message through the FIB mapper and DPA transaction,
  and drives the VPP adapter. Use `--messages N` for deterministic acceptance.
  The DPDK lane preflight is `danos-test/integration/run_vpp_dpdk_lane.sh`; it
  reports SKIP when no PCI/user-space driver is exposed and never treats a
  kernel or mock dataplane as DPDK evidence. It accepts `DPDK_PCI_DRIVER` as
  `vfio-pci`, `uio_pci_generic` or `igb_uio` for compatibility testing.

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
   traffic, withdraw, reconnect and restart recovery. A real FRR static
   two-next-hop replay now produces a VPP FIB load-balance with two resolved
   buckets (`172.17.0.1` and `172.17.0.3`). The bridge now invokes
   the programming sweep after each committed ZAPI transaction so deleted
   routes are withdrawn from VPP using the programmed ledger copy. With
   `--reconnect`, a live add/delete run processed two route transactions and
   the VPP FIB returned to its default drop entry after the sweep.
   A two-next-hop traffic topology using the trixie containers at `.3` and
   `.4` then produced two resolved FIB buckets and VPP ping to
   `203.0.113.10` completed 5/5 packets with zero loss.
   VPP restart replay logic is implemented. The acceptance harness now waits
   for `/run/vpp/api.sock`, reapplies the shared `0666` socket mode after a
   container restart, and fails explicitly if the socket remains unusable.
   The bridge also forgets stale programmed-ledger entries after a VPP
   handshake and replays desired routes; an idle health probe reproduced
   replay with `attempted=1 failed=0`.
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
Containerized trixie runners can use
`danos-test/integration/run_vpp_dpdk_container_lane.sh`; the current runner
has PCI `0000:04:00.0`, VFIO and hugepages. That device is a Realtek
`10ec:8168` RTL8111/8168; both a VFIO-bound and a historical
`uio_pci_generic` probe reached DPDK but VPP rejected it as an unsupported PCI
device, so changing the kernel binding does not make it a DPDK dataplane.
The checked-in software validation image intentionally contains
`plugins { plugin dpdk_plugin.so { disable } }`; enabling it is reserved for
the dedicated PCI/VFIO runner and is not mixed into the software baseline.
The dedicated startup template is
`danos-test/integration/vpp-dpdk-startup.conf`; its BDF must match the
runner-selected VFIO-bound device before launch.

QEMU/KVM VMXNET3 smoke evidence is now available: launching
`build/danos-open-live.iso` with QEMU's `-device vmxnet3` produced guest PCI
`0000:00:02.0 [15ad:07b0]` in the DANOS kernel log. The live ISO currently
starts `mgrd` without a VPP runtime (`VPP not reachable`), so this proves the
VMXNET3 PCI exposure but is not yet DPDK forwarding evidence. The next QEMU
step is to boot a trixie guest image containing VPP, DPDK plugin and the
startup template, then bind this guest NIC and run the packet/performance
lane inside the guest.

The same ISO has now passed a QEMU `e1000` network smoke test using the
project-bundled `e1000.ko`: guest `eth0` received `10.0.2.15/24`, the link
reported 1000 Mbps full duplex, and the kernel FIB contained the user-mode NAT
default route. This confirms that a non-VMXNET3 QEMU NIC can test the DANOS
live networking path; it does not count as VPP/DPDK evidence because the ISO
still reports `VPP not reachable`.

A QEMU `virtio-net-pci` boot initially exposed missing `net_failover` and
`failover` module dependencies. The ISO builder now includes the available
dependency modules (and tolerates kernel-built-in variants); the rebuilt ISO
successfully creates `eth0` and configures `10.0.2.15/24` under QEMU
virtio-net. Both e1000 and virtio-net are now validated generic QEMU NICs.

The archived DANOS dataplane confirms the historical fallback: its
`vyatta-dataplane/tools/vplane-uio` selects `vfio-pci` when IOMMU groups are
safe and falls back to `uio_pci_generic` when a group overlaps storage; the
`dpdk-kmods` source separately packages `igb_uio` as DKMS. The same source
contains VMXNET3 device support (`15ad:07b0`) and the image includes
`librte-net-vmxnet3-25`, making a VMware VMXNET3 guest the correct remaining
DPDK acceptance target.

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

## 2026-09-22 progress update

FRR 10.3 ifindex-only nexthops are now decoded without fabricating a gateway
address. The VPP encoder correspondingly emits `FIB_PATH_TYPE_API_ATTACHED`
for a zero gateway, preventing VPP from attempting an ARP/probe for
`0.0.0.0`; non-zero gateways retain the normal recursive path. Regression
coverage now includes both the FRR type-2 gateway payload and the attached
VPP path wire encoding. The local build and all 34 CTest cases pass, and the
changes are pushed as `a05df70` and `c119960`.

The next privileged acceptance step is route add/withdraw against a VPP
instance with a socket accessible to the host bridge process, followed by
`vppctl` FIB inspection and packet traffic. The previous runtime attempt was
blocked by the mounted socket permissions (`VPP adapter setup failed:
Permission denied`), so this is an environment harness issue rather than a
counted dataplane pass. Use the standard integration harness with explicit
socket ownership/permissions, then test ECMP, restart recovery and deletion.

The VPP API gate has since been corrected against the Debian trixie VPP
26.10 generated `fib_types.api`: the path contains `rpf_id`, u32 type/flags/
proto fields and a 28-byte nexthop union. Zero-gateway FRR ifindex-only paths
use `FIB_API_PATH_FLAG_RESOLVE_VIA_ATTACHED`; the former malformed layout was
causing VPP message truncation or SIGSEGV. With a VPP tap interface (`tap0`,
sw_if_index 1) and `DANOS_VPP_IFINDEX_MAP=2:1`, a real FRR 10.3
`ZEBRA_FRR_REDISTRIBUTE_ROUTE_ADD` reached VPP successfully:
`processed=1 failed=0`, and `vppctl show ip fib` showed the attached route.
The corrected encoder and regression test are pushed as `4596e90`; all 34
local tests pass.

The remaining live gap is a distinct FRR route-delete notification: the
standard static route add/replay is observed, but the current FRR container
run did not emit a command-32 event after `no ip route`, so the VPP route
withdraw cannot yet be claimed. This is now isolated to FRR registration/
staticd notification behavior, not VPP framing or route-add programming.

The delete gate was subsequently reproduced with the bridge `--reconnect`
mode, which keeps the ZAPI session alive across FRR idle/EOF transitions.
The trace contained a real `zapi command=32` for the static route,
`programming sweep: withdrawn=1 failed=0`, and the final VPP FIB lookup had
only the default drop path. This proves the FRR→DPA→VPP add/withdraw
lifecycle for the tested route; reconnect mode is required for this
containerized FRR runtime because zebra closes the client socket during its
idle transition.

ECMP was also exercised with two FRR static nexthops for
`203.0.113.0/24`. VPP reported two attached paths (`172.17.0.1` and
`172.17.0.2`, both on `tap0`) and a `dpo-load-balance` with two buckets; the
subsequent command-32 withdrawal removed the programmed route. This is
control-plane/FIB ECMP evidence; packet distribution still requires a
two-port privileged lane and remains separate from this socket-level result.

The live recovery race is now handled by bounded replay retries after the
VPP API reconnect. In the privileged test, VPP was restarted, `tap0` was
recreated, and the bridge replayed the desired route ledger until
`198.51.100.0/24` reappeared in the VPP FIB; the run completed with
`processed=2 failed=0`. This closes the software socket-level restart gate;
the persistent-interface variant still belongs to the QEMU/DPDK lane.

The QEMU e1000 two-port ISO lane was rebuilt after correcting the VPP 26.10
socket-server configuration to `socksvr { default }`; the unsupported
`api-listen` directive had prevented `api.sock` creation. With
`-enable-kvm -cpu host`, the guest reports both `/run/vpp/api.sock` and
`stats.sock` ready, both `GigabitEthernet0/2/0` and `GigabitEthernet0/3/0`
up, the `30.30.30.0/24` two-next-hop FIB, and mgrd connected to VPP. DPDK
reports flow-offload error `-38` for QEMU e1000 and disables only flow
offload; the ports and control-plane FIB remain operational. The rebuilt
artifact is `build/danos-vpp-dpdk-e1000-2port-api2.iso`.

The ISO now has an opt-in traffic gate (`VPP_DPDK_TRAFFIC_TEST=1`). The
resulting `build/danos-vpp-dpdk-e1000-2port-traffic.iso` was booted with
QEMU user peers on `10.10.0.0/24` and `10.20.0.0/24`; VPP reported
`VPP-DPDK-PING-0 PASS` and `VPP-DPDK-PING-1 PASS` for three probes each.
Both DPDK e1000 interfaces were up while the two-path ECMP FIB was present.
This closes the basic two-port packet reachability gate; flow-offload error
`-38` remains a QEMU emulation limitation and is explicitly downgraded by
VPP without disabling the ports.

The host DPDK preflight remains explicitly blocked: no host `vpp` binary is
available, only 19 hugepages exist and all are consumed, and the idle
Realtek PCI Ethernet function `04:00.0` (`10ec:8168`) is still owned by the
kernel `r8169` driver. It has not been rebound automatically because doing so
requires privileged host PCI state changes. The QEMU e1000 lane remains the
reproducible software DPDK acceptance path.
