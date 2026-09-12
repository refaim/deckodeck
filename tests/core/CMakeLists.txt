file(GLOB_RECURSE AVIFPVD_CORE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(core_tests ${AVIFPVD_CORE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
# avifpvd_pvd supplies pvd::Progress, which core::FileSession calls.
target_link_libraries(core_tests PRIVATE avifpvd_core avifpvd_pvd doctest::doctest avifpvd_options)
add_test(NAME core_tests COMMAND core_tests)
