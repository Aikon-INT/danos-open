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
