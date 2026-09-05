cmake_minimum_required(VERSION 3.16)

add_library(danos-core STATIC
  src/object/object_registry.c
  src/object/dpa_objects.c
  src/state/state_store.c
  src/transaction/transaction.c
  src/event/event_bus.c
  src/reconciler/reconciler.c
  src/capability/capability_registry.c
)
target_include_directories(danos-core PUBLIC include)
target_link_libraries(danos-core PUBLIC danos-dpa)
target_link_libraries(danos-core PRIVATE pthread)

if(DANOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
