file(GLOB_RECURSE EXR_CORE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# The EXR decisions without the library: channels, parts, windows, compositing, encoding, colour
# matching and the describer.
add_executable(exr_core_tests ${EXR_CORE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(exr_core_tests PRIVATE exr_core doctest::doctest pvdkit_options)
add_test(NAME exr_core_tests COMMAND exr_core_tests)
