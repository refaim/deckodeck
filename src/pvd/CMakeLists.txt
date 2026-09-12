file(GLOB_RECURSE AVIFPVD_PVD_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
set(AVIFPVD_PVD_EXPORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Exports.cpp")
list(REMOVE_ITEM AVIFPVD_PVD_SOURCES ${AVIFPVD_PVD_EXPORT_SOURCES})

# Identity of the plugin for the VERSIONINFO resource. The version itself comes from
# project(VERSION) in the top-level CMakeLists.txt; PluginConstants.hpp static_asserts that its
# kPluginVersion agrees with the generated header, so a bump cannot land in only one place.
set(AVIFPVD_AUTHOR "Roman Kharitonov" CACHE STRING "CompanyName in the VERSIONINFO resource")
set(AVIFPVD_COPYRIGHT "Copyright (C) 2026 Roman Kharitonov"
    CACHE STRING "LegalCopyright in the VERSIONINFO resource")

# The resource's Comments repeat what pvdPluginInfo reports at run time (DefaultPlugin.cpp:
# "AVIF decoder: <avif::libraryVersions()>; static build"); the e2e version test pins the two
# strings equal. libavif's version is what find_package(libavif) found; dav1d's comes from its
# pkg-config file and libyuv's from its version header, both installed by vcpkg next to it.
find_file(AVIFPVD_DAV1D_PC NAMES dav1d.pc PATH_SUFFIXES lib/pkgconfig REQUIRED)
file(STRINGS "${AVIFPVD_DAV1D_PC}" _avifpvd_dav1d_version REGEX "^Version: ")
string(REGEX REPLACE "^Version: *" "" _avifpvd_dav1d_version "${_avifpvd_dav1d_version}")
find_file(AVIFPVD_LIBYUV_VERSION_HEADER NAMES libyuv/version.h PATH_SUFFIXES include REQUIRED)
file(STRINGS "${AVIFPVD_LIBYUV_VERSION_HEADER}" _avifpvd_libyuv_version
     REGEX "^#define LIBYUV_VERSION ")
string(REGEX REPLACE "^#define LIBYUV_VERSION +" "" _avifpvd_libyuv_version
       "${_avifpvd_libyuv_version}")
if(NOT libavif_VERSION OR NOT _avifpvd_dav1d_version OR NOT _avifpvd_libyuv_version)
  message(FATAL_ERROR "Library versions for the VERSIONINFO comments were not found: "
                      "libavif '${libavif_VERSION}', dav1d '${_avifpvd_dav1d_version}', "
                      "libyuv '${_avifpvd_libyuv_version}'")
endif()
set(AVIFPVD_PLUGIN_COMMENTS
    "AVIF decoder: libavif ${libavif_VERSION}, dav1d ${_avifpvd_dav1d_version}, libyuv ${_avifpvd_libyuv_version}; static build")
unset(_avifpvd_dav1d_version)
unset(_avifpvd_libyuv_version)

# The three strings land inside C string literals and the .rc; escape backslashes and quotes so an
# override such as -DAVIFPVD_AUTHOR=`He said "hi"` cannot break the generated header.
foreach(_avifpvd_var AVIFPVD_AUTHOR AVIFPVD_COPYRIGHT AVIFPVD_PLUGIN_COMMENTS)
    string(REPLACE "\\" "\\\\" _avifpvd_escaped "${${_avifpvd_var}}")
    string(REPLACE "\"" "\\\"" _avifpvd_escaped "${_avifpvd_escaped}")
    set(${_avifpvd_var}_ESCAPED "${_avifpvd_escaped}")
endforeach()
unset(_avifpvd_var)
unset(_avifpvd_escaped)
configure_file(Version.hpp.in "${PROJECT_BINARY_DIR}/generated/pvd/Version.hpp" @ONLY)
# Every target links avifpvd_options, so every target can include "pvd/Version.hpp".
target_include_directories(avifpvd_options INTERFACE "${PROJECT_BINARY_DIR}/generated")

# The marshalling layer: Shim, ContextHandle, Firewall, Progress. It depends on the boundary
# headers only (pvd/Types.hpp, pvd/Plugin.hpp, core/Error.hpp), never on a core symbol, so it does
# not link avifpvd_core; executables that also use core add that link line themselves.
add_library(avifpvd_pvd STATIC ${AVIFPVD_PVD_SOURCES})
set_property(TARGET avifpvd_pvd PROPERTY AVIFPVD_EXPORT_SOURCES "${AVIFPVD_PVD_EXPORT_SOURCES}")
target_include_directories(avifpvd_pvd PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(avifpvd_pvd PUBLIC avifpvd_options)

# AVIF.pvd: the composition root (Exports.cpp) over the three static layers. The module definition
# file is the complete export list; nothing in the tree uses __declspec(dllexport). Its bare names
# are the exported names on both architectures: on x86 the linker resolves each one to the
# __stdcall-decorated symbol (`pvdInit` -> `_pvdInit@0`) itself, so the file needs no aliases.
add_library(
  avifpvd SHARED
  ${AVIFPVD_PVD_EXPORT_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/AVIF.def"
  "${CMAKE_CURRENT_SOURCE_DIR}/AVIF.rc")
set_target_properties(avifpvd PROPERTIES OUTPUT_NAME AVIF PREFIX "" SUFFIX ".pvd")
target_link_libraries(avifpvd PRIVATE avifpvd_pvd avifpvd_core avifpvd_adapters avifpvd_options)
