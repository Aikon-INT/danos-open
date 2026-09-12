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
