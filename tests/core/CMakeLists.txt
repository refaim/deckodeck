file(GLOB_RECURSE PVDKIT_CORE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(core_tests ${PVDKIT_CORE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
# pvdkit_pvd supplies pvd::Progress, which core::FileSession calls.
target_link_libraries(core_tests PRIVATE pvdkit_core pvdkit_pvd doctest::doctest pvdkit_options)
add_test(NAME core_tests COMMAND core_tests)
