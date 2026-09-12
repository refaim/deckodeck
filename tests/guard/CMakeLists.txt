file(GLOB_RECURSE AVIFPVD_GUARD_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(guard_tests ${AVIFPVD_GUARD_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_compile_definitions(guard_tests PRIVATE AVIFPVD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(guard_tests PRIVATE doctest::doctest avifpvd_options)
add_test(NAME guard_tests COMMAND guard_tests)
