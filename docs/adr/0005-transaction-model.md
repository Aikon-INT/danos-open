# ADR-0005: Transaction-Based Configuration Model

**Status**: Accepted
**Date**: 2024-12-01

## Context

DANOS-Open needs a configuration model that:

1. Provides atomicity (all-or-nothing application of changes)
2. Supports validation before commit (reject invalid configurations)
3. Enables concurrent readers (control plane protocols read state while
   configuration is being applied)
4. Allows rollback on failure
5. Provides verification (confirm programmed state matches desired)

Traditional NOS approaches:
- **Immediate apply**: Each command takes effect immediately. No atomicity.
- **Candidate config**: Build a candidate, validate, commit. Better but
  no transaction semantics (no concurrent transactions).
- **Database-style transactions**: Full ACID, but complex.

## Decision

Adopt a **transaction-based configuration model** with the lifecycle:

```
OPEN → PREPARE → VALIDATE → COMMIT → VERIFY → DONE
```

Key properties:
1. **Multi-read, single-writer**: Multiple transactions can read
   concurrently; only one can commit at a time.
2. **Capability validation at VALIDATE**: Backend capabilities are
   checked before commit, not at OPEN.
3. **Global write mutex at COMMIT**: Serializes commits with a 1-second
   timeout to prevent starvation.
4. **State model**: CONFIG → DESIRED → PROGRAMMED → OPER
   - CONFIG: User-provided configuration
   - DESIRED: Validated and committed configuration
   - PROGRAMMED: What's actually in the dataplane
   - OPER: Operational state from dataplane
5. **Reconciliation**: Background thread detects DESIRED ≠ PROGRAMMED
   and re-programs.

## Consequences

**Positive**:
- Atomic configuration changes (no partial application)
- Validation before commit prevents invalid state
- Concurrent readers don't block configuration
- Reconciliation self-heals drift

**Negative**:
- Transaction overhead (mutex contention under high write load)
- 1-second commit timeout may be too short for large configurations
- Reconciliation adds complexity
- State model (4 states) is more complex than 2-state (config/oper)

**Neutral**:
- Transaction IDs are 64-bit, monotonically increasing
- Failed transactions are aborted (no rollback needed, nothing committed)

## Alternatives Considered

1. **Immediate apply**: Rejected — no atomicity, no validation.
2. **Candidate config (no transactions)**: Rejected — no concurrency,
   no timeout semantics.
3. **Full ACID transactions**: Rejected — overkill for NOS configuration,
   durability is handled by config persistence, not transaction log.
4. **CRDT-based**: Rejected — configuration is not naturally
   conflict-free.
