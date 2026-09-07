# danos-observability CMakeLists.txt

set(OBS_SOURCES
    src/prometheus.c
    src/log.c
)

add_library(danos-observability ${OBS_SOURCES})
target_include_directories(danos-observability PUBLIC include)

enable_testing()
add_executable(observability_test tests/test_observability.c)
target_link_libraries(observability_test danos-observability)
add_test(NAME observability COMMAND observability_test)
