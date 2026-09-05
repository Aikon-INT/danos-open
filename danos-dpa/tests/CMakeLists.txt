cmake_minimum_required(VERSION 3.16)

add_executable(dpa_test_errors test_dpa_errors.c)
target_link_libraries(dpa_test_errors PRIVATE danos-dpa)
target_compile_options(dpa_test_errors PRIVATE -UNDEBUG)
add_test(NAME dpa_errors COMMAND dpa_test_errors)

add_executable(dpa_test_version test_dpa_version.c)
target_link_libraries(dpa_test_version PRIVATE danos-dpa)
target_compile_options(dpa_test_version PRIVATE -UNDEBUG)
add_test(NAME dpa_version COMMAND dpa_test_version)

add_executable(dpa_test_capability test_dpa_capability.c)
target_link_libraries(dpa_test_capability PRIVATE danos-dpa)
target_compile_options(dpa_test_capability PRIVATE -UNDEBUG)
add_test(NAME dpa_capability COMMAND dpa_test_capability)
