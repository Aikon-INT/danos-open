# CMake generated Testfile for 
# Source directory: /home/aikon/danos-open/danos-mgmt/tests
# Build directory: /home/aikon/danos-open/build-asan/danos-mgmt/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(cli "/home/aikon/danos-open/build-asan/danos-mgmt/tests/cli_test")
set_tests_properties(cli PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;6;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
add_test(gnmi "/home/aikon/danos-open/build-asan/danos-mgmt/tests/gnmi_test")
set_tests_properties(gnmi PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;11;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
add_test(netconf "/home/aikon/danos-open/build-asan/danos-mgmt/tests/netconf_test")
set_tests_properties(netconf PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;16;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
add_test(gnmi_proto "/home/aikon/danos-open/build-asan/danos-mgmt/tests/gnmi_proto_test")
set_tests_properties(gnmi_proto PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;21;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
add_test(hpack "/home/aikon/danos-open/build-asan/danos-mgmt/tests/hpack_test")
set_tests_properties(hpack PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;26;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
add_test(gnmi_grpc "/home/aikon/danos-open/build-asan/danos-mgmt/tests/gnmi_grpc_test")
set_tests_properties(gnmi_grpc PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;31;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
add_test(frontends_consistent "/home/aikon/danos-open/build-asan/danos-mgmt/tests/frontends_consistent_test")
set_tests_properties(frontends_consistent PROPERTIES  _BACKTRACE_TRIPLES "/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;36;add_test;/home/aikon/danos-open/danos-mgmt/tests/CMakeLists.txt;0;")
