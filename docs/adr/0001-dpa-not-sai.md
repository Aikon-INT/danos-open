# ADR-0001: Use DPA as Data Plane Abstraction (not SAI)

**Status**: Accepted
**Date**: 2024-12-01

## Context

DANOS-Open needs a data plane abstraction layer to decouple control plane
logic from the underlying dataplane implementation. The existing SAI
(Switch Abstraction Interface) standard is:

1. Tied to ASIC SDK semantics (object IDs, hardware resource allocation)
2. Lacks transaction support (operations are immediate, not atomic)
3. Has no capability discovery (backends must support all objects)
4. Cannot express modern features (EVPN, SR-MPLS, gRIBI) cleanly

DANOS-Open's principles explicitly state "DPA ≠ SAI" — the abstraction
must be backend-agnostic (works with VPP, DPDK, P4, kernel) and
capability-driven.

## Decision

Define a new Data Plane Abstraction (DPA) API with:

1. **Capability-based**: Backends advertise supported object types and
   features. Core validates at transaction VALIDATE phase.
2. **Transactional**: OPEN → PREPARE → VALIDATE → COMMIT → VERIFY → DONE
   with multi-read/single-writer concurrency.
3. **Backend-agnostic**: Works with VPP (binary API), DPDK (rte_flow),
   P4 (BMv2 runtime), Linux kernel (netlink).
4. **Three equivalent interfaces**: C ABI (for in-process), Protobuf+gRPC
   (for distributed), YANG+NETCONF/gNMI (for management).

## Consequences

**Positive**:
- Clean separation between control and data plane
- New backends can be added without changing control plane code
- Transactions provide atomicity and rollback semantics
- Capability discovery prevents unsupported operations at commit time

**Negative**:
- New API to learn (not industry-standard like SAI)
- Mapping layer adds one level of indirection
- Must maintain three interface definitions (C, proto, YANG)

**Neutral**:
- DPA is the single source of truth for data plane operations
- All backends must implement the same DPA contract

## Alternatives Considered

1. **SAI**: Rejected — too ASIC-centric, no transactions, no capabilities.
2. **OpenConfig + gNMI only**: Rejected — management-plane only, no
   in-process C ABI for high-performance control plane integration.
3. **P4 Runtime as universal API**: Rejected — P4 is dataplane-specific,
   cannot express control plane semantics (BGP, OSPF).
4. **No abstraction (direct VPP API)**: Rejected — couples control plane
   to VPP, violates "backend-agnostic" principle.
