file(GLOB_RECURSE AVIFPVD_E2E_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# e2e_tests loads the built AVIF.pvd with LoadLibraryW and drives its eight exports like the
# host; it links none of the plugin's static libraries (avifpvd_options only carries flags and
# the include path comes from the pvd headers it needs for the SDK types).
add_executable(e2e_tests ${AVIFPVD_E2E_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
add_dependencies(e2e_tests avifpvd)
target_include_directories(e2e_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
# version.lib serves the VERSIONINFO read-back test; it is linked into this test only, never into
# the plugin (check_imports keeps AVIF.pvd at KERNEL32.dll).
target_link_libraries(e2e_tests PRIVATE doctest::doctest avifpvd_options version)
target_compile_definitions(
  e2e_tests
  PRIVATE
    AVIFPVD_PLUGIN_PATH="$<TARGET_FILE:avifpvd>"
    AVIFPVD_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
add_test(NAME e2e_tests COMMAND e2e_tests)

# The import-table policy is part of the Release definition of done (AGENTS.md): AVIF.pvd must
# import KERNEL32.dll and nothing else. CONFIGURATIONS (rather than a CMAKE_BUILD_TYPE test at
# configure time) keeps the test on the multi-config `Visual Studio 17 2022 -T ClangCL` fallback
# too; ctest selects it through `-C Release`, which the release test preset passes.
find_program(AVIFPVD_POWERSHELL NAMES pwsh powershell REQUIRED)
add_test(
  NAME check_imports
  CONFIGURATIONS Release
  COMMAND "${AVIFPVD_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
          "${PROJECT_SOURCE_DIR}/scripts/check-imports.ps1" -Path "$<TARGET_FILE:avifpvd>")

# The export-table policy: exactly the eight PVD entry points under their bare names. On x86 the
# __stdcall symbols are `_pvdInit@0` etc.; the .def file makes the linker export them undecorated,
# and this test keeps it that way on both architectures (same Release-only registration as above).
add_test(
  NAME check_exports
  CONFIGURATIONS Release
  COMMAND "${AVIFPVD_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
          "${PROJECT_SOURCE_DIR}/scripts/check-exports.ps1" -Path "$<TARGET_FILE:avifpvd>")
