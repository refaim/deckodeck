file(GLOB_RECURSE PVDKIT_ADAPTER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# The shared Win32 adapter on real temporary files (normal, empty, missing, non-ASCII, long path).
add_executable(adapter_tests ${PVDKIT_ADAPTER_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(adapter_tests PRIVATE pvdkit_win doctest::doctest pvdkit_options)
add_test(NAME adapter_tests COMMAND adapter_tests)
