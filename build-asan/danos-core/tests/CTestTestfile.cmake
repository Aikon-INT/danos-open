# CMake generated Testfile for 
# Source directory: /home/aikon/danos-open/danos-core/tests
# Build directory: /home/aikon/danos-open/build-asan/danos-core/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(core "/home/aikon/danos-open/build-asan/danos-core/tests/core_test")
set_tests_properties(core PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;13;add_test;/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;0;")
add_test(wal "/home/aikon/danos-open/build-asan/danos-core/tests/wal_test")
set_tests_properties(wal PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;19;add_test;/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;0;")
add_test(antiflap "/home/aikon/danos-open/build-asan/danos-core/tests/antiflap_test")
set_tests_properties(antiflap PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;25;add_test;/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;0;")
add_test(persist "/home/aikon/danos-open/build-asan/danos-core/tests/persist_test")
set_tests_properties(persist PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;30;add_test;/home/aikon/danos-open/danos-core/tests/CMakeLists.txt;0;")
