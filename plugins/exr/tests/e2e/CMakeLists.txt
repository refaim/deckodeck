file(GLOB_RECURSE EXR_E2E_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
set(EXR_SEQUENCE_EXPECTATIONS "${CMAKE_CURRENT_SOURCE_DIR}/SequenceExpectations.cpp")
list(REMOVE_ITEM EXR_E2E_TEST_SOURCES "${EXR_SEQUENCE_EXPECTATIONS}")

# LoadLibraryW(EXR.pvd) driven like the host on every fixture, plus the VERSIONINFO read-back and
# the import/export-table checks (exr_check_imports, exr_check_exports). The C++ OpenEXR library
# is linked into the e2e test only, to write the 4K timing inputs into %TEMP%.
pvdkit_add_plugin_e2e_tests(
  exr
  FIXTURES "${EXR_FIXTURE_DIR}"
  SEQUENCE_EXPECTATIONS "${EXR_SEQUENCE_EXPECTATIONS}"
  SOURCES ${EXR_E2E_TEST_SOURCES})
target_include_directories(exr_e2e_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(exr_e2e_tests PRIVATE OpenEXR::OpenEXR)
