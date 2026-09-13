# CMake generated Testfile for 
# Source directory: /home/aikon/danos-open/danos-test
# Build directory: /home/aikon/danos-open/build-asan/danos-test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(conformance "/home/aikon/danos-open/build-asan/danos-test/conformance_test")
set_tests_properties(conformance PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;11;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(perf_baseline "/home/aikon/danos-open/build-asan/danos-test/perf_baseline")
set_tests_properties(perf_baseline PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;17;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(bgp_converge "/home/aikon/danos-open/build-asan/danos-test/bgp_converge_test")
set_tests_properties(bgp_converge PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;23;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(sw_fwd_baseline "/home/aikon/danos-open/build-asan/danos-test/sw_fwd_baseline_test")
set_tests_properties(sw_fwd_baseline PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;29;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(txn_scale "/home/aikon/danos-open/build-asan/danos-test/txn_scale_test")
set_tests_properties(txn_scale PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;49;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(fuzz_decoders "/home/aikon/danos-open/build-asan/danos-test/fuzz_decoders")
set_tests_properties(fuzz_decoders PROPERTIES  TIMEOUT "300" _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;55;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(concurrent_conns "/home/aikon/danos-open/build-asan/danos-test/concurrent_conns_test")
set_tests_properties(concurrent_conns PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;61;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
add_test(programming_pipeline "/home/aikon/danos-open/build-asan/danos-test/programming_pipeline_test")
set_tests_properties(programming_pipeline PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-test/CMakeLists.txt;68;add_test;/home/aikon/danos-open/danos-test/CMakeLists.txt;0;")
