file(GLOB_RECURSE AVIF_ADAPTER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# The libavif adapter on the fixtures, the static link of libavif+dav1d, and the composed
# production plugin (avif_composition supplies pvd::makePlugin).
add_executable(avif_adapter_tests ${AVIF_ADAPTER_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(avif_adapter_tests PRIVATE avif_composition doctest::doctest pvdkit_options)
target_compile_definitions(
  avif_adapter_tests PRIVATE PVDKIT_FIXTURE_DIR="${AVIF_FIXTURE_DIR}")
add_test(NAME avif_adapter_tests COMMAND avif_adapter_tests)
