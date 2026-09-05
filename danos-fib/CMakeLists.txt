cmake_minimum_required(VERSION 3.16)
# danos-fib: FRR FIB Adapter (C1-C4)

add_library(danos-fib STATIC
  src/zapi/zapi_parse.c
  src/mapper/zapi_mapper.c
  src/session/zebra_session.c
)
target_include_directories(danos-fib PUBLIC src)
target_link_libraries(danos-fib PUBLIC danos-dpa danos-core)

if(DANOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
