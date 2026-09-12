cmake_minimum_required(VERSION 3.16)
# danos-mgmt: Management plane (E1-E4)
add_library(danos-mgmt STATIC
  src/cli/cli.c
  src/gnmi/gnmi.c
  src/gnmi/gnmi_proto.c
  src/gnmi/hpack.c
  src/gnmi/hpack_huffman.c
  src/gnmi/gnmi_grpc.c
  src/netconf/netconf.c
)
target_include_directories(danos-mgmt PUBLIC src)
target_link_libraries(danos-mgmt PUBLIC danos-dpa danos-core)

if(DANOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
