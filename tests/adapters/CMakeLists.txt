file(GLOB_RECURSE AVIFPVD_ADAPTER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(adapter_tests ${AVIFPVD_ADAPTER_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
# avifpvd_pvd supplies pvd::Progress, which core::FileSession (reached through DefaultPlugin) calls.
target_link_libraries(adapter_tests PRIVATE avifpvd_adapters avifpvd_pvd doctest::doctest avifpvd_options)
target_compile_definitions(
  adapter_tests PRIVATE AVIFPVD_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
add_test(NAME adapter_tests COMMAND adapter_tests)
