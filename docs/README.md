# DANOS-Open Documentation Map

**`docs/` is the single documentation root.** Everything a reader or
contributor needs lives here; everything else is an archive.

## Active documents

| Document | Purpose |
|----------|---------|
| [ADR index](adr/README.md) | Architecture decisions 0001–0007 (read first) |
| [Interop DoD](interop/v0.4_interop_dod.md) | External-interop acceptance (I/V/K groups) |
| [Compat matrix](interop/v0.5_compat_matrix.md) | Frozen protocol versions + upgrade policy |
| [Threading contract](threading.md) | Threads, shared state, lock rules |
| [Release notes](release/) | Per-tag notes (v0.6.0+) |
| [Project status](project-status.md) | Current assessment, roadmap and acceptance policy |
| [v0.16 acceptance matrix](v0.16-acceptance-matrix.md) | Single current rc1 acceptance record |
| [v0.16 performance schema](v0.16-performance-result-schema.md) | Machine-readable QEMU/VMware/PCI result fields |
| [v0.16 next-stage plan](v0.16-next-stage-plan.md) | Release closure sequence and deferred extensions |

## Entry points outside docs/

- `README.md` — quick start, module map, verified interoperability
- `CONTRIBUTING.md` — gates, rules, where things live
- `TODO.md` — public backlog (environment/feature/quality)

## Archive

- `archive/v1.1/` — the frozen v1.1 architecture specification set
  (design-era documents; superseded in part by the ADRs and the
  interop/compat records above). Do not update; cite as history.

## Rules

1. New design decisions → `docs/adr/NNNN-*.md` (no other location).
2. New acceptance criteria → extend `docs/interop/v0.4_interop_dod.md`
   or a new dated file in `docs/interop/`.
3. Anything frozen gets moved under `docs/archive/` with a note in the
   doc map above. No parallel "current" copies.
