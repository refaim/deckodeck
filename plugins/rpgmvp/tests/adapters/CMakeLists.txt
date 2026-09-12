file(GLOB_RECURSE RPGMVP_ADAPTER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(rpgmvp_adapter_tests ${RPGMVP_ADAPTER_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(
  rpgmvp_adapter_tests
  PRIVATE rpgmvp_adapter
          rpgmvp_composition
          pvdkit_leakcheck
          doctest::doctest
          pvdkit_options)
target_compile_definitions(
  rpgmvp_adapter_tests PRIVATE PVDKIT_FIXTURE_DIR="${RPGMVP_FIXTURE_DIR}")
add_test(NAME rpgmvp_adapter_tests COMMAND rpgmvp_adapter_tests)
