file(GLOB_RECURSE AVIFPVD_PVD_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
set(AVIFPVD_PVD_EXPORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Exports.cpp")
list(REMOVE_ITEM AVIFPVD_PVD_SOURCES ${AVIFPVD_PVD_EXPORT_SOURCES})

# The marshalling layer: Shim, ContextHandle, Firewall, Progress. It depends on the boundary
# headers only (pvd/Types.hpp, pvd/Plugin.hpp, core/Error.hpp), never on a core symbol, so it does
# not link avifpvd_core; executables that also use core add that link line themselves.
add_library(avifpvd_pvd STATIC ${AVIFPVD_PVD_SOURCES})
set_property(TARGET avifpvd_pvd PROPERTY AVIFPVD_EXPORT_SOURCES "${AVIFPVD_PVD_EXPORT_SOURCES}")
target_include_directories(avifpvd_pvd PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(avifpvd_pvd PUBLIC avifpvd_options)

# AVIF.pvd: the composition root (Exports.cpp) over the three static layers. The module definition
# file is the complete export list; nothing in the tree uses __declspec(dllexport).
add_library(avifpvd SHARED ${AVIFPVD_PVD_EXPORT_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/AVIF.def")
set_target_properties(avifpvd PROPERTIES OUTPUT_NAME AVIF PREFIX "" SUFFIX ".pvd")
target_link_libraries(avifpvd PRIVATE avifpvd_pvd avifpvd_core avifpvd_adapters avifpvd_options)
