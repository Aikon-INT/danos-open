# CMake toolchain file for aarch64 cross-compilation
#
# Usage:
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64.cmake
#   cmake --build build-arm -j$(nproc)
#
# Requires: gcc-aarch64-linux-gnu package on the build host.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Sysroot (optional: set if using a non-default target sysroot)
# set(CMAKE_SYSROOT /path/to/aarch64/sysroot)

# Find root paths for target libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Architecture-specific flags
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
