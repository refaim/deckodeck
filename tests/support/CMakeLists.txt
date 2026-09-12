# Shared test support (never linked into a plugin):
#
# - pvdkit_leakcheck: the process-wide resource accounting behind every leak gate
#   (LeakCheck.hpp/.cpp - heap walk, handle count, private bytes, warm-up/measure/report) and the
#   deterministic hostile corpus generator (HostileCorpus.hpp/.cpp). Linked by adapter_tests,
#   every <id>_adapter_tests and every <id>_leak_tests.
# - leakcheck_tests: their self-tests - a deliberately leaked block and a deliberately leaked
#   handle must be charged exactly, warm-up must not; the corpus must be deterministic and every
#   mutant must differ from its source.
# - leak/: the host-like leak scenarios, compiled into every plugin's <id>_leak_tests by
#   pvdkit_add_plugin_e2e_tests (cmake/pvdkit-plugin.cmake), which reads the source list from the
#   PVDKIT_LEAK_SCENARIO_SOURCES property below.
add_library(pvdkit_leakcheck STATIC LeakCheck.cpp LeakCheck.hpp HostileCorpus.cpp HostileCorpus.hpp)
target_include_directories(pvdkit_leakcheck PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pvdkit_leakcheck PUBLIC doctest::doctest pvdkit_options)

file(GLOB PVDKIT_LEAK_SCENARIO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/leak/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/leak/*.hpp")
set_target_properties(pvdkit_leakcheck PROPERTIES PVDKIT_LEAK_SCENARIO_SOURCES "${PVDKIT_LEAK_SCENARIO_SOURCES}")

add_executable(leakcheck_tests LeakCheckTests.cpp HostileCorpusTests.cpp "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
target_link_libraries(leakcheck_tests PRIVATE pvdkit_leakcheck doctest::doctest pvdkit_options)
add_test(NAME leakcheck_tests COMMAND leakcheck_tests)

# In-process host-sequence replay over pvd::Shim. The driver itself is framework-free; one
# doctest executable per plugin links it to that plugin's composition root (registered by
# pvdkit_add_plugin_sequence_tests) so the duplicate pvd::makePlugin symbols never meet.
add_library(pvdkit_sequence STATIC sequence/SequenceDriver.cpp sequence/SequenceDriver.hpp)
target_include_directories(pvdkit_sequence PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pvdkit_sequence PRIVATE pvdkit_pvd pvdkit_core pvdkit_options)
