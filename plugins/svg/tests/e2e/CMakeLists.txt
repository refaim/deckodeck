file(GLOB_RECURSE SVG_E2E_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
set(SVG_SEQUENCE_EXPECTATIONS "${CMAKE_CURRENT_SOURCE_DIR}/SequenceExpectations.cpp")
set(SVG_COMPOSITION_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/CompositionTests.cpp")
list(REMOVE_ITEM SVG_E2E_TEST_SOURCES "${SVG_SEQUENCE_EXPECTATIONS}" "${SVG_COMPOSITION_TESTS}")

pvdkit_add_plugin_e2e_tests(
  svg
  FIXTURES "${SVG_FIXTURE_DIR}"
  SEQUENCE_EXPECTATIONS "${SVG_SEQUENCE_EXPECTATIONS}"
  SOURCES ${SVG_E2E_TEST_SOURCES})

target_sources(svg_sequence_tests PRIVATE "${SVG_COMPOSITION_TESTS}")
# The sequence executable links the composition root and therefore resvg.lib directly; the YY
# overlay directory belongs on every executable that resolves Rust's Win7 imports.
target_link_directories(svg_sequence_tests BEFORE PRIVATE "${SVG_YY_ROOT}")
