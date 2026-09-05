cmake_minimum_required(VERSION 3.16)

add_executable(fib_test_parse test_zapi_parse.c)
target_link_libraries(fib_test_parse PRIVATE danos-fib danos-core danos-dpa pthread)
target_compile_options(fib_test_parse PRIVATE -UNDEBUG)
add_test(NAME fib_parse COMMAND fib_test_parse)

add_executable(fib_test_mapper test_zapi_mapper.c)
target_link_libraries(fib_test_mapper PRIVATE danos-fib danos-core danos-dpa pthread)
target_compile_options(fib_test_mapper PRIVATE -UNDEBUG)
add_test(NAME fib_mapper COMMAND fib_test_mapper)

# C4: E2E test with mock zebra
add_executable(mock_zebra mock_zebra.c)
target_link_libraries(mock_zebra PRIVATE danos-fib danos-dpa pthread)
target_compile_options(mock_zebra PRIVATE -UNDEBUG)

add_executable(fib_e2e_test test_fib_e2e.c)
target_link_libraries(fib_e2e_test PRIVATE danos-fib danos-core danos-dpa pthread)
target_compile_options(fib_e2e_test PRIVATE -UNDEBUG)
add_test(NAME fib_e2e COMMAND fib_e2e_test)
