# danos-security CMakeLists.txt

set(SECURITY_SOURCES
    src/rbac.c
    src/copp.c
    src/audit.c
)

add_library(danos-security ${SECURITY_SOURCES})
target_include_directories(danos-security PUBLIC include)

# Tests
enable_testing()
add_executable(security_test tests/test_security.c)
target_link_libraries(security_test danos-security)
add_test(NAME security COMMAND security_test)
