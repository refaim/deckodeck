file(GLOB_RECURSE EXR_ADAPTER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# The OpenEXR adapter on the fixtures (every fixture's meta, pixels pinned against the Python
# reference and the C++ library's own RgbaInputFile for luminance-chroma files), the composed
# production plugin, and the unit-level leak gate (LeakTests.cpp, pvdkit_leakcheck). The C++
# OpenEXR library is linked into this test only (never into the plugin) as the reference reader.
add_executable(exr_adapter_tests ${EXR_ADAPTER_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(exr_adapter_tests PRIVATE exr_composition pvdkit_leakcheck OpenEXR::OpenEXR doctest::doctest pvdkit_options)
target_include_directories(exr_adapter_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(exr_adapter_tests PRIVATE PVDKIT_FIXTURE_DIR="${EXR_FIXTURE_DIR}")
add_test(NAME exr_adapter_tests COMMAND exr_adapter_tests)
