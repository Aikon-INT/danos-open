cmake_minimum_required(VERSION 3.16)
project(danos-open VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build options
option(DANOS_BUILD_TESTS "Build unit tests" ON)
option(DANOS_BUILD_VPP   "Build VPP backend" ON)
option(DANOS_BUILD_FIB   "Build FRR FIB adapter" ON)
option(DANOS_BUILD_MGMT  "Build management plane" ON)

# Enable testing early so add_test() in subdirectories is registered
if(DANOS_BUILD_TESTS)
  enable_testing()
endif()

# Common flags
add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wno-unused-parameter
  -Wno-unused-but-set-variable
)
add_compile_definitions(_GNU_SOURCE)

# Include paths
include_directories(${CMAKE_SOURCE_DIR})

# Subdirectories
add_subdirectory(danos-dpa)
add_subdirectory(danos-core)

if(DANOS_BUILD_FIB)
  add_subdirectory(danos-fib)
endif()

if(DANOS_BUILD_VPP)
  add_subdirectory(danos-vpp)
endif()

if(DANOS_BUILD_MGMT)
  add_subdirectory(danos-mgmt)
endif()

add_subdirectory(danos-security)

if(DANOS_BUILD_TESTS)
  add_subdirectory(danos-test)
endif()

# Version info
message(STATUS "DANOS-Open v${PROJECT_VERSION}")
message(STATUS "  Tests:  ${DANOS_BUILD_TESTS}")
message(STATUS "  VPP:    ${DANOS_BUILD_VPP}")
message(STATUS "  FIB:    ${DANOS_BUILD_FIB}")
message(STATUS "  Mgmt:   ${DANOS_BUILD_MGMT}")
