file(GLOB_RECURSE PVDKIT_PVD_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
set(PVDKIT_PVD_EXPORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Exports.cpp")
list(REMOVE_ITEM PVDKIT_PVD_SOURCES ${PVDKIT_PVD_EXPORT_SOURCES})

# The marshalling layer: Shim, ContextHandle, Firewall, Progress. It depends on the boundary
# headers only (pvd/Types.hpp, pvd/Plugin.hpp, core/Error.hpp), never on a core symbol, so it does
# not link pvdkit_core; executables that also use core add that link line themselves. It carries
# nothing plugin-specific: the identity a Shim reports comes from its constructor argument.
add_library(pvdkit_pvd STATIC ${PVDKIT_PVD_SOURCES})
target_include_directories(pvdkit_pvd PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(pvdkit_pvd PUBLIC pvdkit_options)
target_compile_definitions(
  pvdkit_pvd PRIVATE $<$<BOOL:${PVDKIT_EXPERIMENT_ICC}>:PVDKIT_EXPERIMENT_ICC=1>)

# The three files every plugin DLL is made of, on top of its own composition root: Exports.cpp
# (the eight forwards, compiled once per plugin against that plugin's generated
# pvd/PluginConstants.hpp), Plugin.def (the export list, identical for every plugin) and
# Plugin.rc (the VERSIONINFO resource, filled from the same generated header).
# pvdkit_add_plugin() in cmake/pvdkit-plugin.cmake reads them from these properties.
set_property(TARGET pvdkit_pvd PROPERTY PVDKIT_EXPORT_SOURCES "${PVDKIT_PVD_EXPORT_SOURCES}")
set_property(TARGET pvdkit_pvd PROPERTY PVDKIT_DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Plugin.def")
set_property(TARGET pvdkit_pvd PROPERTY PVDKIT_RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Plugin.rc")
