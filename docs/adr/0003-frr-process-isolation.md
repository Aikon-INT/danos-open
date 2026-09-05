# ADR-0003: FRR as Control Plane via Process Isolation

**Status**: Accepted
**Date**: 2024-12-01

## Context

DANOS-Open needs routing protocols (BGP, OSPF, IS-IS) for the control
plane. Options:

1. **FRR (Free Range Routing)**: Mature, GPLv2, supports BGP/OSPF/IS-IS/
   BFD/PIM, widely deployed, active community.
2. **Bird**: GPL, good for IGP, weaker BGP.
3. **Custom implementation**: Violates "Do Not Reimplement Mature Protocols".
4. **GoBGP**: BGP only, no IGP.

FRR is GPLv2 while DANOS-Open core is Apache-2.0. Direct linking would
create a licensing conflict.

## Decision

Use **FRR as the control plane, running as a separate process**,
communicating with DANOS-Open core via Zebra ZAPI protocol over Unix
socket. This achieves:

1. **License isolation**: FRR (GPLv2) runs as separate process, no
   library linking. DANOS-Open core (Apache-2.0) is not a derivative
   work of FRR.
2. **Process isolation**: FRR crash does not bring down DANOS-Open core.
3. **Standard protocol**: ZAPI is FRR's documented inter-process protocol.
4. **Upgrade independence**: FRR can be upgraded without rebuilding core.

The FIB Adapter (danos-fib) translates ZAPI messages to DPA operations.

## Consequences

**Positive**:
- Clean license boundary (Apache-2.0 core, GPLv2 FRR process)
- FRR maturity and feature set available immediately
- Process isolation improves fault tolerance
- FRR community support and documentation

**Negative**:
- IPC overhead (Unix socket + serialization)
- Must maintain ZAPI parser as FRR evolves
- Debugging spans two processes
- FRR configuration model differs from DPA transaction model

**Neutral**:
- FRR runs as a systemd service alongside DANOS-Open core
- ZAPI socket at /var/run/frr/zebra.zserv

## Alternatives Considered

1. **Link FRR as library**: Rejected — GPLv2 contamination of
   Apache-2.0 core.
2. **Bird**: Rejected — weaker BGP, less industry adoption.
3. **Custom BGP/OSPF**: Rejected — violates "Do Not Reimplement Mature
   Protocols" principle.
4. **FRR + gRPC instead of ZAPI**: Rejected — ZAPI is native to FRR,
   adding gRPC would require FRR patches.
