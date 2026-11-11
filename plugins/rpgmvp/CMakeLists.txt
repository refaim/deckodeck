# RPGMVP.pvd: RPG Maker MV/MZ encrypted PNG/WebP decoder, linked statically.
find_package(SPNG CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(WebP CONFIG REQUIRED)

if(NOT SPNG_VERSION OR NOT ZLIB_VERSION OR NOT WebP_VERSION)
  message(FATAL_ERROR "Library versions for VERSIONINFO comments were not found: "
                      "libspng '${SPNG_VERSION}', zlib '${ZLIB_VERSION}', libwebp '${WebP_VERSION}'")
endif()

set(RPGMVP_PLUGIN_COMMENTS
    "RPG Maker MV/MZ encrypted PNG/WebP decoder: libspng ${SPNG_VERSION}, zlib ${ZLIB_VERSION}, libwebp ${WebP_VERSION}; static build")

pvdkit_plugin_identity(
  rpgmvp
  NAME RPGMVP
  VERSION 1.3.0
  PRIORITY 10
  DESCRIPTION "RPG Maker MV/MZ encrypted PNG/WebP decoder plugin for PictureView (Far Manager)"
  COMMENTS "${RPGMVP_PLUGIN_COMMENTS}")

# RPGMVP-specific decisions without libspng: signature/IHDR-CRC detection and the describer.
file(GLOB_RECURSE RPGMVP_CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp" "src/core/*.hpp")
add_library(rpgmvp_core STATIC ${RPGMVP_CORE_SOURCES})
target_include_directories(rpgmvp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rpgmvp_core PUBLIC pvdkit_core pvdkit_options)

file(GLOB_RECURSE RPGMVP_ADAPTER_SOURCES CONFIGURE_DEPENDS "src/adapters/*.cpp" "src/adapters/*.hpp")
add_library(rpgmvp_adapter STATIC ${RPGMVP_ADAPTER_SOURCES})
target_include_directories(rpgmvp_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
  rpgmvp_adapter
  PUBLIC rpgmvp_core pvdkit_core pvdkit_options
         $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static> ZLIB::ZLIB WebP::webpdecoder)

add_library(rpgmvp_composition STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/DefaultPlugin.cpp")
target_link_libraries(
  rpgmvp_composition
  PUBLIC rpgmvp_identity rpgmvp_adapter rpgmvp_core pvdkit_win pvdkit_pvd pvdkit_options)

# Keep the complete combined COPYING/PATENTS notice: its patent heading contains a dashed
# underline that the shared Debian-notice truncation rule would otherwise cut off.
find_file(RPGMVP_WEBP_COPYRIGHT NAMES copyright PATH_SUFFIXES "share/libwebp" REQUIRED)
pvdkit_add_plugin(
  rpgmvp
  LINK rpgmvp_composition
  LICENSES libspng "libspng (BSD-2-Clause)" zlib "zlib (zlib license)"
  LICENSES_FILES "${RPGMVP_WEBP_COPYRIGHT}" "libwebp (BSD-3-Clause and WebM patent grant)")

if(BUILD_TESTING)
  set(RPGMVP_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_subdirectory(tests)
endif()
