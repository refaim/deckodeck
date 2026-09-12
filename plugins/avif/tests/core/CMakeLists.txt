file(GLOB_RECURSE AVIF_CORE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# The AVIF describer, without libavif.
add_executable(avif_core_tests ${AVIF_CORE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(avif_core_tests PRIVATE avif_core doctest::doctest pvdkit_options)
add_test(NAME avif_core_tests COMMAND avif_core_tests)
