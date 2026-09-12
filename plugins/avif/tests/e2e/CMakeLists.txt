file(GLOB_RECURSE AVIF_E2E_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# LoadLibraryW(AVIF.pvd) driven like the host on every fixture, plus the VERSIONINFO read-back and
# the import/export-table checks (avif_check_imports, avif_check_exports).
pvdkit_add_plugin_e2e_tests(
  avif
  FIXTURES "${AVIF_FIXTURE_DIR}"
  SOURCES ${AVIF_E2E_TEST_SOURCES})
