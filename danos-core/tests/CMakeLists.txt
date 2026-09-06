cmake_minimum_required(VERSION 3.16)

add_executable(core_test
  test_main.c
  test_object.c
  test_state.c
  test_transaction.c
  test_event.c
  test_reconciler.c
)
target_link_libraries(core_test PRIVATE danos-core danos-dpa pthread)
target_compile_options(core_test PRIVATE -UNDEBUG)
add_test(NAME core COMMAND core_test)

# B6: WAL persistence test
add_executable(wal_test test_wal.c)
target_link_libraries(wal_test PRIVATE danos-core danos-dpa pthread)
target_compile_options(wal_test PRIVATE -UNDEBUG)
add_test(NAME wal COMMAND wal_test)

# B9: Anti-flap test
add_executable(antiflap_test test_antiflap.c)
target_link_libraries(antiflap_test PRIVATE danos-core danos-dpa pthread)
target_compile_options(antiflap_test PRIVATE -UNDEBUG)
add_test(NAME antiflap COMMAND antiflap_test)
