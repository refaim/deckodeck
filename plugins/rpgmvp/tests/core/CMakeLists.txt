file(GLOB_RECURSE RPGMVP_CORE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_executable(rpgmvp_core_tests ${RPGMVP_CORE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(rpgmvp_core_tests PRIVATE rpgmvp_core doctest::doctest pvdkit_options)
add_test(NAME rpgmvp_core_tests COMMAND rpgmvp_core_tests)
