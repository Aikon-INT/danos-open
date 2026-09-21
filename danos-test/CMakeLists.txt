cmake_minimum_required(VERSION 3.16)

# Conformance test suite (A6)
add_executable(conformance_test
  conformance/conformance_runner.c
  conformance/conformance_cases.c
)
target_link_libraries(conformance_test PRIVATE danos-core danos-dpa pthread)
target_compile_definitions(conformance_test PRIVATE)  # ensure no NDEBUG
target_compile_options(conformance_test PRIVATE -UNDEBUG)
add_test(NAME conformance COMMAND conformance_test)

# Performance baseline (H1-H2)
add_executable(perf_baseline perf/perf_baseline.c)
target_link_libraries(perf_baseline PRIVATE danos-core danos-dpa pthread)
target_compile_options(perf_baseline PRIVATE -UNDEBUG)
add_test(NAME perf_baseline COMMAND perf_baseline)

# H1: BGP convergence simulation (1K routes, target < 5s)
add_executable(bgp_converge_test perf/bgp_converge_test.c)
target_link_libraries(bgp_converge_test PRIVATE danos-fib danos-core danos-dpa pthread)
target_compile_options(bgp_converge_test PRIVATE -UNDEBUG)
add_test(NAME bgp_converge COMMAND bgp_converge_test)

# H2: Software forwarding baseline (veth, kernel path)
add_executable(sw_fwd_baseline_test perf/sw_fwd_baseline_test.c)
target_link_libraries(sw_fwd_baseline_test PRIVATE danos-core danos-dpa pthread)
target_compile_options(sw_fwd_baseline_test PRIVATE -UNDEBUG)
add_test(NAME sw_fwd_baseline COMMAND sw_fwd_baseline_test)

# v0.3 vertical roundtrip: gNMI Set -> persistence -> restart recovery
add_executable(v03_roundtrip_test v03_roundtrip_test.c)
target_link_libraries(v03_roundtrip_test PRIVATE danos-mgmt danos-core danos-dpa pthread)
target_compile_options(v03_roundtrip_test PRIVATE -UNDEBUG)

# standalone gNMI server for external-client interop (gnmic / grpc-go)
add_executable(gnmi_demo_server gnmi_demo_server.c)
target_link_libraries(gnmi_demo_server PRIVATE danos-mgmt danos-core danos-dpa pthread)

# V-group live test: run on a machine with a real VPP (see Interop DoD)
add_executable(vpp_live_test vpp_live_test.c)
target_link_libraries(vpp_live_test PRIVATE danos-vpp danos-core danos-dpa pthread)
target_compile_options(vpp_live_test PRIVATE -UNDEBUG)

# Live FRR zebra -> FIB/DPA -> VPP bridge.  This is intentionally a small
# bounded runner so the same production wiring can be used by acceptance
# harnesses without embedding a second protocol implementation in tests.
add_executable(fib_live_bridge fib_live_bridge.c)
target_link_libraries(fib_live_bridge PRIVATE danos-fib danos-vpp danos-core danos-dpa pthread)
target_compile_options(fib_live_bridge PRIVATE -UNDEBUG)

# v0.5 performance baseline: transaction scale + WAL recovery
add_executable(txn_scale_test perf/txn_scale_test.c)
target_link_libraries(txn_scale_test PRIVATE danos-core danos-dpa pthread)
target_compile_options(txn_scale_test PRIVATE -UNDEBUG)
add_test(NAME txn_scale COMMAND txn_scale_test)

# v0.8: mutational fuzzing of the hand-written decoders (ASAN builds)
add_executable(fuzz_decoders fuzz/fuzz_decoders.c)
target_link_libraries(fuzz_decoders PRIVATE danos-mgmt danos-core danos-dpa pthread)
target_compile_options(fuzz_decoders PRIVATE -UNDEBUG)
if(DANOS_LIBFUZZER)
  target_compile_definitions(fuzz_decoders PRIVATE DANOS_LIBFUZZER)
  target_compile_options(fuzz_decoders PRIVATE -fsanitize=fuzzer,address)
  target_link_options(fuzz_decoders PRIVATE -fsanitize=fuzzer,address)
endif()
add_test(NAME fuzz_decoders COMMAND fuzz_decoders)
set_tests_properties(fuzz_decoders PROPERTIES TIMEOUT 300)

add_executable(concurrent_conns_test scale/concurrent_conns_test.c)
target_link_libraries(concurrent_conns_test PRIVATE danos-mgmt danos-core danos-dpa pthread)
target_compile_options(concurrent_conns_test PRIVATE -UNDEBUG)
add_test(NAME concurrent_conns COMMAND concurrent_conns_test)
set_tests_properties(concurrent_conns PROPERTIES TIMEOUT 120)

# v0.9: programming pipeline end to end (desired -> backend -> PROGRAMMED)
add_executable(programming_pipeline_test integration_test/programming_pipeline_test.c)
target_link_libraries(programming_pipeline_test PRIVATE danos-netlink danos-vpp danos-mgmt danos-core danos-dpa pthread)
target_compile_options(programming_pipeline_test PRIVATE -UNDEBUG)
add_test(NAME programming_pipeline COMMAND programming_pipeline_test)

# v0.12: gNMI composite route write end to end
add_executable(route_composite_test integration_test/route_composite_test.c)
target_link_libraries(route_composite_test PRIVATE danos-netlink danos-vpp danos-mgmt danos-core danos-dpa pthread)
target_compile_options(route_composite_test PRIVATE -UNDEBUG)
add_test(NAME route_composite COMMAND route_composite_test)
