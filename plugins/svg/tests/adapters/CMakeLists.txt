file(GLOB_RECURSE SVG_ADAPTER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(svg_adapter_tests ${SVG_ADAPTER_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(
  svg_adapter_tests
  PRIVATE svg_adapter svg_composition pvdkit_leakcheck doctest::doctest pvdkit_options)
target_compile_definitions(
  svg_adapter_tests PRIVATE PVDKIT_FIXTURE_DIR="${SVG_FIXTURE_DIR}")
# The unit executable links resvg.lib directly, so it also needs the YY-Thunks overlay archives;
# a PRIVATE link directory on the static composition library does not propagate.
target_link_directories(svg_adapter_tests BEFORE PRIVATE "${SVG_YY_ROOT}")
add_test(NAME svg_adapter_tests COMMAND svg_adapter_tests)
