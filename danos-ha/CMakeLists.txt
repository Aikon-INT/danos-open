# danos-ha CMakeLists.txt

set(HA_SOURCES
    src/bfd.c
    src/vrrp.c
    src/supervisor.c
)

add_library(danos-ha ${HA_SOURCES})
target_include_directories(danos-ha PUBLIC include)

enable_testing()
add_executable(ha_test tests/test_ha.c)
target_link_libraries(ha_test danos-ha)
add_test(NAME ha COMMAND ha_test)
