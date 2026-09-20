# DANOS Open v0 15 0

## Scope

This release is the engineering-convergence baseline. It consolidates the
existing DPA, persistence, management and programming-pipeline work and makes
the remaining privileged integration work explicit instead of presenting it as
an unqualified local pass.

## Completed in this cycle

- Added the project status, maturity assessment, roadmap and acceptance policy
  in `docs/project-status.md`.
- Updated `TODO.md` to the v0.15 convergence baseline.
- Fixed the DPA status-code collision where `DANOS_ERR_PERMISSION` and
  `DANOS_ERR_RETRY` both had value 7. Permission is now value 8.
- Added a regression assertion for unique retry and permission status values.
- Added primary interface IPv4/IPv6 prefix fields with YANG/model-path bindings,
  gNMI Get/Set/Delete handling and northbound consistency coverage.
- Added the `DANOS_TSAN` CMake option for a dedicated thread-sanitizer build.
- TSAN exposed and fixed races in event subscription, RPC counters and gNMI
  server shutdown; the `gnmi` and `concurrent_conns` TSAN tests now pass.
- Added an LLVM libFuzzer entry point and CI smoke target for the decoder
  harness; local execution requires clang, which is not installed here.
- Made the WAL performance benchmark path-configurable and added a CI job that
  runs it under `/var/tmp` rather than the default `/tmp` location.
- Added gNMI composite ECMP input via `gateways[]`, multi-member NHGroup
  creation/update and cascade deletion coverage.
- Local workspace-filesystem baseline: 1000-object WAL write 3195.0 ms and
  recovery 17.4 ms; CI remains the authoritative `/var/tmp` real-filesystem
  measurement.
- Rebuilt the complete tree after the status-code and pending gNMI changes.
- Passed the deterministic v0.15 subset: DPA errors, core, WAL, persistence,
  conformance, transaction scale, decoder fuzz loop, programming pipeline and
  composite route tests.

## Acceptance evidence

The local restricted environment passed 9/9 deterministic v0.15 tests. With
host network privileges, the complete registered suite passed 34/34, including
FRR/FIB, VPP protocol, gNMI/gRPC, BGP convergence and concurrent-connection
cases. The restricted-environment failures were therefore classified as
environmental rather than code failures.

## Remaining roadmap work after the v0.15 CTest gate

- Real VPP runtime version and forwarding interop.
- Real K4/K5 namespace and ping verification (the current workspace lacks root
  or user namespaces).
- Route/next-hop/next-hop-group dependency deletion acceptance.
- VPP interface-address messages and multi-address modeling.
- v0.16 ECMP real-dataplane forwarding and FRR BGP/OSPF full-path evidence.
