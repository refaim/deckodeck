file(GLOB_RECURSE PVDKIT_PVD_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
get_target_property(PVDKIT_PVD_EXPORT_SOURCES pvdkit_pvd PVDKIT_EXPORT_SOURCES)

# Exports.cpp is compiled into this executable with the test-provided pvd::makePlugin() definition
# from ExportsTests.cpp and against a test identity generated the way every plugin's is; only the
# plugin DLLs and their e2e tests see a production makePlugin().
pvdkit_plugin_identity(
  pvd_tests
  NAME TestPlugin
  VERSION 9.8.7
  PRIORITY 42
  DESCRIPTION "pvd_tests identity"
  COMMENTS "not a plugin")
add_executable(
  pvd_tests
  ${PVDKIT_PVD_TEST_SOURCES}
  "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp"
  ${PVDKIT_PVD_EXPORT_SOURCES})
target_link_libraries(pvd_tests PRIVATE pvd_tests_identity pvdkit_pvd pvdkit_core doctest::doctest pvdkit_options)
add_test(NAME pvd_tests COMMAND pvd_tests)
