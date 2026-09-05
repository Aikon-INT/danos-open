cmake_minimum_required(VERSION 3.16)

add_executable(vpp_mapper_test test_vpp_mapper.c)
target_link_libraries(vpp_mapper_test PRIVATE danos-vpp danos-dpa pthread)
target_compile_options(vpp_mapper_test PRIVATE -UNDEBUG)
add_test(NAME vpp_mapper COMMAND vpp_mapper_test)
