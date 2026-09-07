# danos-compat: OcNOS Compatibility Layer

add_library(danos-compat STATIC
    src/cli_translator.c
    src/config_import.c
)
target_include_directories(danos-compat PUBLIC include)

# Unit test
add_executable(compat_test tests/test_compat.c)
target_link_libraries(compat_test PRIVATE danos-compat)
add_test(NAME compat COMMAND compat_test)
