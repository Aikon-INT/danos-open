cmake_minimum_required(VERSION 3.16)

add_executable(cli_test test_cli.c)
target_link_libraries(cli_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(cli_test PRIVATE -UNDEBUG)
add_test(NAME cli COMMAND cli_test)

add_executable(gnmi_test test_gnmi.c)
target_link_libraries(gnmi_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(gnmi_test PRIVATE -UNDEBUG)
add_test(NAME gnmi COMMAND gnmi_test)

add_executable(netconf_test test_netconf.c)
target_link_libraries(netconf_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(netconf_test PRIVATE -UNDEBUG)
add_test(NAME netconf COMMAND netconf_test)

add_executable(gnmi_proto_test test_gnmi_proto.c)
target_link_libraries(gnmi_proto_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(gnmi_proto_test PRIVATE -UNDEBUG)
add_test(NAME gnmi_proto COMMAND gnmi_proto_test)

add_executable(hpack_test test_hpack.c)
target_link_libraries(hpack_test PRIVATE danos-mgmt pthread)
target_compile_options(hpack_test PRIVATE -UNDEBUG)
add_test(NAME hpack COMMAND hpack_test)

add_executable(gnmi_grpc_test test_gnmi_grpc.c)
target_link_libraries(gnmi_grpc_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(gnmi_grpc_test PRIVATE -UNDEBUG)
add_test(NAME gnmi_grpc COMMAND gnmi_grpc_test)
