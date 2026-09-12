cmake_minimum_required(VERSION 3.16)

add_executable(vpp_mapper_test test_vpp_mapper.c)
target_link_libraries(vpp_mapper_test PRIVATE danos-vpp danos-dpa pthread)
target_compile_options(vpp_mapper_test PRIVATE -UNDEBUG)
add_test(NAME vpp_mapper COMMAND vpp_mapper_test)

add_executable(vpp_api_test test_vpp_api.c)
target_link_libraries(vpp_api_test PRIVATE danos-vpp danos-dpa pthread)
target_compile_options(vpp_api_test PRIVATE -UNDEBUG)
add_test(NAME vpp_api COMMAND vpp_api_test)

add_executable(vpp_proto_test test_vpp_proto.c mock_vpp_server.c)
target_link_libraries(vpp_proto_test PRIVATE danos-vpp danos-dpa pthread)
target_compile_options(vpp_proto_test PRIVATE -UNDEBUG)
add_test(NAME vpp_proto COMMAND vpp_proto_test)
