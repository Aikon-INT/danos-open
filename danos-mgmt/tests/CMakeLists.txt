cmake_minimum_required(VERSION 3.16)

add_executable(cli_test test_cli.c)
target_link_libraries(cli_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(cli_test PRIVATE -UNDEBUG)
add_test(NAME cli COMMAND cli_test)

add_executable(gnmi_test test_gnmi.c)
target_link_libraries(gnmi_test PRIVATE danos-mgmt danos-dpa pthread)
target_compile_options(gnmi_test PRIVATE -UNDEBUG)
add_test(NAME gnmi COMMAND gnmi_test)
