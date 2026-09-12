cmake_minimum_required(VERSION 3.16)
# danos-mgrd: the system control-plane daemon (v0.6)
add_executable(danos-mgrd mgrd.c)
target_link_libraries(danos-mgrd PRIVATE
  danos-mgmt danos-vpp danos-core danos-observability danos-dpa pthread)
