file(GLOB_RECURSE SVG_CORE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(svg_core_tests ${SVG_CORE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(svg_core_tests PRIVATE svg_core doctest::doctest pvdkit_options)
add_test(NAME svg_core_tests COMMAND svg_core_tests)
