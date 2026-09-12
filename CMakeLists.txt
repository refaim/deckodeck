cmake_minimum_required(VERSION 3.28)

# pvdkit: shared libraries for PictureView (Far Manager) decoder plugins plus one directory per
# plugin under plugins/. Versions belong to the plugins (pvdkit_plugin_identity in each
# plugins/<id>/CMakeLists.txt), not to the kit.
# RC: src/pvd/Plugin.rc (VERSIONINFO), compiled by llvm-rc from the chainload toolchain once per
# plugin.
project(pvdkit LANGUAGES CXX RC)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PVDKIT_COVERAGE "Instrument every target for llvm-cov" OFF)
# Identity shared by every plugin's VERSIONINFO resource (CompanyName, LegalCopyright).
set(PVDKIT_AUTHOR "Roman Kharitonov" CACHE STRING "CompanyName in the VERSIONINFO resources")
set(PVDKIT_COPYRIGHT "Copyright (C) 2026 Roman Kharitonov"
    CACHE STRING "LegalCopyright in the VERSIONINFO resources")
# Empty builds every plugins/<id>/ directory; a semicolon-separated list of ids restricts the
# build (and, through cmake/vcpkg-root.cmake, the vcpkg manifest features) to those plugins.
set(PVDKIT_PLUGINS "" CACHE STRING "Plugin ids to build (empty = all under plugins/)")

add_library(pvdkit_options INTERFACE)
target_compile_options(
  pvdkit_options
  INTERFACE
    /clang:-std=c++23
    /W4
    /WX
    /permissive-
    /utf-8
    /EHsc
    /Zc:preprocessor
    # clang-cl 19 ignores /Zc:preprocessor; do not promote that driver note to an error.
    -Wno-unused-command-line-argument
    $<$<BOOL:${PVDKIT_COVERAGE}>:/Od>
    $<$<BOOL:${PVDKIT_COVERAGE}>:/Zi>
    $<$<BOOL:${PVDKIT_COVERAGE}>:-fprofile-instr-generate>
    $<$<BOOL:${PVDKIT_COVERAGE}>:-fcoverage-mapping>)
target_link_options(
  pvdkit_options
  INTERFACE
    $<$<CONFIG:Release>:/DEBUG:NONE>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<BOOL:${PVDKIT_COVERAGE}>:-fprofile-instr-generate>)

if(PVDKIT_COVERAGE AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  # Every instrumented object names the profile runtime in a /DEFAULTLIB directive, and lld-link
  # looks for it in its own resource directory. The x64-hosted LLVM that cross-compiles the x86
  # build ships only clang_rt.profile-x86_64.lib there, so clang falls back to the arch-less name
  # clang_rt.profile.lib, which exists nowhere. The i386 runtime of the same LLVM version comes
  # with the x86-hosted LLVM of the same Build Tools (VC/Tools/Llvm/lib/clang/<v>/lib/windows):
  # drop the unresolvable default and link that file explicitly. (x64 needs nothing: its runtime
  # is found next to lld-link.)
  get_filename_component(_pvdkit_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  file(GLOB _pvdkit_profile_runtime
       "${_pvdkit_llvm_bin}/../../lib/clang/*/lib/windows/clang_rt.profile-i386.lib")
  if(NOT _pvdkit_profile_runtime)
    message(FATAL_ERROR "clang_rt.profile-i386.lib was not found under ${_pvdkit_llvm_bin}/../../lib/clang")
  endif()
  list(GET _pvdkit_profile_runtime 0 _pvdkit_profile_runtime)
  target_link_options(pvdkit_options INTERFACE /NODEFAULTLIB:clang_rt.profile.lib)
  target_link_libraries(pvdkit_options INTERFACE "${_pvdkit_profile_runtime}")
  unset(_pvdkit_llvm_bin)
  unset(_pvdkit_profile_runtime)
endif()

include(CTest)
include(cmake/pvdkit-plugin.cmake)

add_subdirectory(src)
if(BUILD_TESTING)
  find_package(doctest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()

# Every plugins/<id>/CMakeLists.txt is a plugin; CONFIGURE_DEPENDS re-globs when one is added.
file(GLOB _pvdkit_plugin_lists CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/plugins/*/CMakeLists.txt")
set(_pvdkit_built_plugins "")
foreach(_pvdkit_plugin_list IN LISTS _pvdkit_plugin_lists)
  get_filename_component(_pvdkit_plugin_dir "${_pvdkit_plugin_list}" DIRECTORY)
  get_filename_component(_pvdkit_plugin_id "${_pvdkit_plugin_dir}" NAME)
  if(PVDKIT_PLUGINS STREQUAL "" OR _pvdkit_plugin_id IN_LIST PVDKIT_PLUGINS)
    add_subdirectory("${_pvdkit_plugin_dir}")
    list(APPEND _pvdkit_built_plugins "${_pvdkit_plugin_id}")
  endif()
endforeach()
if(NOT _pvdkit_built_plugins)
  message(FATAL_ERROR "No plugin selected: PVDKIT_PLUGINS='${PVDKIT_PLUGINS}', found ${_pvdkit_plugin_lists}")
endif()
foreach(_pvdkit_plugin_id IN LISTS PVDKIT_PLUGINS)
  if(NOT _pvdkit_plugin_id IN_LIST _pvdkit_built_plugins)
    message(FATAL_ERROR "PVDKIT_PLUGINS names '${_pvdkit_plugin_id}', but plugins/${_pvdkit_plugin_id}/CMakeLists.txt does not exist")
  endif()
endforeach()
message(STATUS "pvdkit plugins: ${_pvdkit_built_plugins}")
unset(_pvdkit_plugin_lists)
unset(_pvdkit_plugin_list)
unset(_pvdkit_plugin_dir)
unset(_pvdkit_plugin_id)
unset(_pvdkit_built_plugins)
