file(GLOB_RECURSE PVDKIT_GUARD_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# Scans src/ and every plugins/*/src/ for the ownership and layering rules (AGENTS.md rules 2-4).
add_executable(guard_tests ${PVDKIT_GUARD_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_compile_definitions(guard_tests PRIVATE PVDKIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(guard_tests PRIVATE doctest::doctest pvdkit_options)
add_test(NAME guard_tests COMMAND guard_tests)
