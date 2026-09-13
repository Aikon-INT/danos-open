# CMake generated Testfile for 
# Source directory: /home/aikon/danos-open/danos-fib/tests
# Build directory: /home/aikon/danos-open/build-asan/danos-fib/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(fib_parse "/home/aikon/danos-open/build-asan/danos-fib/tests/fib_test_parse")
set_tests_properties(fib_parse PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;6;add_test;/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;0;")
add_test(fib_mapper "/home/aikon/danos-open/build-asan/danos-fib/tests/fib_test_mapper")
set_tests_properties(fib_mapper PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;11;add_test;/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;0;")
add_test(fib_e2e "/home/aikon/danos-open/build-asan/danos-fib/tests/fib_e2e_test")
set_tests_properties(fib_e2e PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;21;add_test;/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;0;")
add_test(fib_e2e_multi "/home/aikon/danos-open/build-asan/danos-fib/tests/fib_e2e_multi_test")
set_tests_properties(fib_e2e_multi PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;27;add_test;/home/aikon/danos-open/danos-fib/tests/CMakeLists.txt;0;")
