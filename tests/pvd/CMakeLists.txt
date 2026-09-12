file(GLOB_RECURSE AVIFPVD_PVD_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
get_target_property(AVIFPVD_PVD_EXPORT_SOURCES avifpvd_pvd AVIFPVD_EXPORT_SOURCES)

# Exports.cpp is compiled into this executable with the test-provided pvd::makePlugin() definition
# from ExportsTests.cpp; only AVIF.pvd and e2e_tests see the production one.
add_executable(
  pvd_tests
  ${AVIFPVD_PVD_TEST_SOURCES}
  "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp"
  ${AVIFPVD_PVD_EXPORT_SOURCES})
target_link_libraries(pvd_tests PRIVATE avifpvd_pvd avifpvd_core doctest::doctest avifpvd_options)
add_test(NAME pvd_tests COMMAND pvd_tests)
