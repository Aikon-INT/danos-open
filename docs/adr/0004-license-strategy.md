# ADR-0004: Apache-2.0 License for Core, GPLv2 for FRR

**Status**: Accepted
**Date**: 2024-12-01

## Context

DANOS-Open needs a licensing strategy that:

1. Allows broad adoption (commercial and non-commercial)
2. Is compatible with dependencies: FRR (GPLv2), VPP (Apache-2.0),
   DPDK (BSD-3), P4 (Apache-2.0)
3. Encourages community contribution
4. Does not force derivative works to open source (permissive)
5. Maintains license compatibility with FRR's GPLv2

## Decision

Adopt **Apache-2.0 for all DANOS-Open core code**, with FRR running as
a separate process (GPLv2) via process isolation (see ADR-0003).

| Component | License | Rationale |
|-----------|---------|-----------|
| danos-core | Apache-2.0 | Permissive, broad adoption |
| danos-dpa | Apache-2.0 | API should be freely usable |
| danos-vpp | Apache-2.0 | VPP is Apache-2.0 |
| danos-fib | Apache-2.0 | ZAPI parser is clean-room |
| danos-mgmt | Apache-2.0 | Management plane |
| danos-models | Apache-2.0 | YANG models |
| danos-test | Apache-2.0 | Test framework |
| FRR (external) | GPLv2 | Process-isolated, not linked |

## Consequences

**Positive**:
- Apache-2.0 is permissive: commercial use without copyleft
- Patent grant clause protects contributors and users
- Compatible with VPP, DPDK, P4 licenses
- FRR GPLv2 contained by process isolation

**Negative**:
- Must ensure no FRR code is copied into core
- Must maintain clean-room ZAPI parser (no FRR header inclusion)
- Contributor License Agreement (CLA) recommended for patent safety

**Neutral**:
- LICENSE file at repo root contains Apache-2.0 full text
- FRR is a system dependency, not a subdirectory

## Alternatives Considered

1. **GPLv2 for everything**: Rejected — limits commercial adoption,
   incompatible with VPP's Apache-2.0 (would require GPLv2 VPP fork).
2. **BSD-2/3**: Rejected — no patent grant clause, less protective
   than Apache-2.0.
3. **MIT**: Rejected — no patent grant, too permissive for enterprise.
4. **MPL-2.0**: Rejected — file-level copyleft complicates linking.
