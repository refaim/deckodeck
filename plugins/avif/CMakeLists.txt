# AVIF.pvd: AVIF decoder over libavif (dav1d + libyuv), linked statically.
find_package(libavif CONFIG REQUIRED)

# The pvdPluginInfo comments repeat what DefaultPlugin.cpp reports at run time ("AVIF decoder:
# <avif::libraryVersions()>; static build"); the e2e version test pins the two strings equal.
# libavif's version is what find_package(libavif) found; dav1d's comes from its pkg-config file
# and libyuv's from its version header, both installed by vcpkg next to it.
find_file(AVIF_DAV1D_PC NAMES dav1d.pc PATH_SUFFIXES lib/pkgconfig REQUIRED)
file(STRINGS "${AVIF_DAV1D_PC}" _avif_dav1d_version REGEX "^Version: ")
string(REGEX REPLACE "^Version: *" "" _avif_dav1d_version "${_avif_dav1d_version}")
find_file(AVIF_LIBYUV_VERSION_HEADER NAMES libyuv/version.h PATH_SUFFIXES include REQUIRED)
file(STRINGS "${AVIF_LIBYUV_VERSION_HEADER}" _avif_libyuv_version REGEX "^#define LIBYUV_VERSION ")
string(REGEX REPLACE "^#define LIBYUV_VERSION +" "" _avif_libyuv_version "${_avif_libyuv_version}")
if(NOT libavif_VERSION OR NOT _avif_dav1d_version OR NOT _avif_libyuv_version)
  message(FATAL_ERROR "Library versions for the VERSIONINFO comments were not found: "
                      "libavif '${libavif_VERSION}', dav1d '${_avif_dav1d_version}', "
                      "libyuv '${_avif_libyuv_version}'")
endif()

# The one source of the plugin's identity (name, version, priority): the generated
# pvd/PluginConstants.hpp feeds Exports.cpp, DefaultPlugin.cpp, the VERSIONINFO resource and the
# package manifest.
pvdkit_plugin_identity(
  avif
  NAME AVIF
  VERSION 1.0.0
  PRIORITY 10
  DESCRIPTION "AVIF decoder plugin for PictureView (Far Manager)"
  COMMENTS "AVIF decoder: libavif ${libavif_VERSION}, dav1d ${_avif_dav1d_version}, libyuv ${_avif_libyuv_version}; static build")
unset(_avif_dav1d_version)
unset(_avif_libyuv_version)

# AVIF-specific decisions without libavif: the describer (CICP names, transform summary).
file(GLOB_RECURSE AVIF_CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp" "src/core/*.hpp")
add_library(avif_core STATIC ${AVIF_CORE_SOURCES})
target_include_directories(avif_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(avif_core PUBLIC pvdkit_core pvdkit_options)

# The libavif adapter. The vcpkg `avif` target carries the configuration-correct static dav1d
# archive, and libavif-config.cmake's find_dependency(libyuv) defines the `yuv` target it links.
file(GLOB_RECURSE AVIF_ADAPTER_SOURCES CONFIGURE_DEPENDS "src/adapters/*.cpp" "src/adapters/*.hpp")
add_library(avif_adapter STATIC ${AVIF_ADAPTER_SOURCES})
target_include_directories(avif_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(avif_adapter PUBLIC avif yuv pvdkit_core pvdkit_options)

# The composition root (pvd::makePlugin over win::FileSource, avif::DecoderFactory,
# avif::Describer and core::CodecPlugin), a library so the plugin's tests can call it too.
add_library(avif_composition STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/DefaultPlugin.cpp")
target_link_libraries(avif_composition PUBLIC avif_identity avif_adapter avif_core pvdkit_win pvdkit_pvd pvdkit_options)

pvdkit_add_plugin(
  avif
  LINK avif_composition
  README package/README.txt.in
  LICENSES libavif "libavif (BSD-2-Clause)" dav1d "dav1d (BSD-2-Clause)" libyuv "libyuv (BSD-3-Clause)")

if(BUILD_TESTING)
  set(AVIF_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_subdirectory(tests)
endif()
