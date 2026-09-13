# Contributing to DANOS-Open

## Quick start
```bash
cmake -B build && cmake --build build -j$(nproc)
cd build && ctest
bash ../danos-test/integration/release_check.sh
```

## Gates (all must pass before a PR)
1. `ctest` — full suite (32+ tests)
2. `bash danos-test/integration/run_v0.4_interop.sh` — gnmic interop
   (needs Go; see script header)
3. ASAN/UBSAN build: `cmake -B build-asan -DDANOS_SANITIZE=ON` + ctest
   + `build-asan/danos-test/fuzz_decoders`

## Where things live
- Documentation map / rules: `docs/README.md`
- Architecture decisions: `docs/adr/` (read 0001-0007 first)
- Interop acceptance: `docs/interop/v0.4_interop_dod.md`
- Version/compat policy: `docs/interop/v0.5_compat_matrix.md`
- Threading contract: `docs/threading.md`
- Pending work: `TODO.md`

## Rules
- Every protocol change needs a wire-level test (exact bytes or RFC
  vector) — see vpp_proto_test / gnmi_proto_test / hpack_test
- Every adapter/backend needs a mock for offline testing
- Hand-written parsers are fuzz targets: extend fuzz_decoders.c
- Update `docs/threading.md` when touching threads
