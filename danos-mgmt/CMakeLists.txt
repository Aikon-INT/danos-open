cmake_minimum_required(VERSION 3.16)
# danos-mgmt: Management plane (E1-E4)
add_library(danos-mgmt STATIC
  cli/cli_stub.c
)
target_link_libraries(danos-mgmt PUBLIC danos-dpa danos-core)
