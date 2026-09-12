# Shared tests. tests/e2e/ holds the host driver and the VERSIONINFO test that every plugin's
# e2e executable compiles in (pvdkit_add_plugin_e2e_tests); it defines no target of its own.
# tests/support/ is the leak accounting (pvdkit_leakcheck, with its own self-tests) plus the
# host-like leak scenarios that the same function compiles into every plugin's <id>_leak_tests.
add_subdirectory(support)
add_subdirectory(core)
add_subdirectory(pvd)
add_subdirectory(adapters)
add_subdirectory(guard)
