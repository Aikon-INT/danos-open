# danos-dpa: DPA public API library
# Provides error code strings, version negotiation, capability registry.

add_library(danos-dpa STATIC
  src/dpa_errors.c
  src/dpa_version.c
  src/dpa_capability.c
)
target_include_directories(danos-dpa PUBLIC include)

# Conformance test stub library (links against dpa header only)
if(DANOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
