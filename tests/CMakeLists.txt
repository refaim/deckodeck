# Shared tests. tests/e2e/ holds the host driver and the VERSIONINFO test that every plugin's
# e2e executable compiles in (pvdkit_add_plugin_e2e_tests); it defines no target of its own.
add_subdirectory(core)
add_subdirectory(pvd)
add_subdirectory(adapters)
add_subdirectory(guard)
