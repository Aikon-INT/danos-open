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
