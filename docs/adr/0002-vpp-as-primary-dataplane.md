# ADR-0002: VPP as Primary Dataplane Backend

**Status**: Accepted
**Date**: 2024-12-01

## Context

DANOS-Open needs a primary dataplane backend for v0.1. Candidates:

1. **VPP**: Mature, high-performance, rich feature set (L2/L3/L4/ACL/QoS/
   MPLS), binary API, plugin ecosystem, FD.io community.
2. **DPDK**: Low-level packet processing, no built-in L3 routing, requires
   significant custom code.
3. **Linux kernel**: Limited performance, no advanced features (EVPN, SR).
4. **P4/BMv2**: For prototyping only, not production performance.

## Decision

Adopt **VPP as the primary dataplane backend** for DANOS-Open v0.1 and
beyond. VPP provides:

- Binary API for DPA mapper integration
- Stat segment for operational state
- Rich feature set matching DPA object model
- Proven performance (10M+ pps per core)
- Active community (FD.io, Cisco TAC support)

DPDK and P4 remain as alternative backends for specialized use cases
(DPDK for custom packet processing, P4 for programmable pipelines).

## Consequences

**Positive**:
- Immediate access to L2/L3/L4/ACL/QoS/MPLS features
- Binary API is stable and well-documented
- Stat segment provides fast operational state queries
- Plugin architecture allows extension without forking

**Negative**:
- VPP is a large dependency (~50MB binary)
- Binary API version compatibility must be managed
- VPP's internal threading model must be respected
- Some advanced features (EVPN) require additional configuration

**Neutral**:
- VPP runs as a separate process, communicates via shared memory
- DPA mapper translates DPA objects to VPP binary API calls

## Alternatives Considered

1. **DPDK only**: Rejected — too low-level, would require reimplementing
   L3 routing, ACL, QoS. Violates "Do Not Reimplement Mature Protocols".
2. **Linux kernel**: Rejected — insufficient performance for production NOS.
3. **P4 only**: Rejected — not production-ready, no built-in features.
4. **Multiple backends with no primary**: Rejected — dilutes focus,
   slows v0.1 delivery.
