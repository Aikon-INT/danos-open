cmake_minimum_required(VERSION 3.16)
# danos-vpp: VPP Backend (D1-D7)
add_library(danos-vpp STATIC
  src/api/vpp_wire.c
  src/api/vpp_api.c
  src/api/vpp_stat.c
  src/api/vpp_msgs.c
  src/capability/vpp_capability.c
  src/mapper/vpp_mapper.c
)
target_include_directories(danos-vpp PUBLIC src)
target_link_libraries(danos-vpp PUBLIC danos-dpa danos-core)

if(DANOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
