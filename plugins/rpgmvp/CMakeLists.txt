# RPGMVP.pvd: RPG Maker MV/MZ encrypted PNG decoder over libspng, linked statically.
find_package(SPNG CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

option(PVDKIT_RPGMVP_DEEP_OUTPUT "Emit experimental 16-bit BGRA output for deep RPGMVP sources" OFF)

if(NOT SPNG_VERSION OR NOT ZLIB_VERSION)
  message(FATAL_ERROR "Library versions for VERSIONINFO comments were not found: "
                      "libspng '${SPNG_VERSION}', zlib '${ZLIB_VERSION}'")
endif()

set(RPGMVP_PLUGIN_COMMENTS
    "RPG Maker MV/MZ encrypted PNG decoder: libspng ${SPNG_VERSION}, zlib ${ZLIB_VERSION}; static build")
if(PVDKIT_RPGMVP_DEEP_OUTPUT)
  string(APPEND RPGMVP_PLUGIN_COMMENTS " [experiment: deep output]")
endif()

pvdkit_plugin_identity(
  rpgmvp
  NAME RPGMVP
  VERSION 1.0.1
  PRIORITY 10
  DESCRIPTION "RPG Maker MV/MZ encrypted PNG decoder plugin for PictureView (Far Manager)"
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
         $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static> ZLIB::ZLIB)

add_library(rpgmvp_composition STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/DefaultPlugin.cpp")
target_compile_definitions(
  rpgmvp_composition PRIVATE PVDKIT_RPGMVP_DEEP_OUTPUT=$<BOOL:${PVDKIT_RPGMVP_DEEP_OUTPUT}>)
target_link_libraries(
  rpgmvp_composition
  PUBLIC rpgmvp_identity rpgmvp_adapter rpgmvp_core pvdkit_win pvdkit_pvd pvdkit_options)

pvdkit_add_plugin(
  rpgmvp
  LINK rpgmvp_composition
  README package/README.txt.in
  LICENSES libspng "libspng (BSD-2-Clause)" zlib "zlib (zlib licence)")

if(BUILD_TESTING)
  set(RPGMVP_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_subdirectory(tests)
endif()
