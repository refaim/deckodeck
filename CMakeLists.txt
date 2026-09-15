cmake_minimum_required(VERSION 3.28)

# deckodeck (https://github.com/refaim/deckodeck): shared libraries for PictureView (Far Manager)
# decoder plugins plus one directory per plugin under plugins/. The shared PVD layer underneath
# keeps its original name, pvdkit (see docs/BUILD.md); every pvdkit_* CMake target, variable, function,
# preset, script parameter, directory and file name is unaffected by the product name. Versions
# belong to the plugins (pvdkit_plugin_identity in each plugins/<id>/CMakeLists.txt), not to the
# kit.
# RC: src/pvd/Plugin.rc (VERSIONINFO), compiled by llvm-rc from the chainload toolchain once per
# plugin.
project(deckodeck LANGUAGES CXX RC)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PVDKIT_COVERAGE "Instrument every target for llvm-cov" OFF)
# The `asan` preset (x64, RelWithDebInfo so /MT and the release ports stay): AddressSanitizer on
# every target including the plugin DLLs, unoptimised and with symbols so a report names the
# line. LeakSanitizer is not part of it: clang 19's Windows runtime answers "detect_leaks is not
# supported on this platform" (docs/ARCHITECTURE.md par. 4), so the leak gate stays the level-1
# accounting of the leak tests and this preset is the memory-error gate. The vcpkg ports are not
# instrumented (vcpkg builds them without the flag); only ASan's interceptors see them.
option(PVDKIT_ASAN "Build every target with AddressSanitizer (x64 only)" OFF)
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
    # Control Flow Guard (BinSkim BA2008): clang-cl instruments indirect calls, lld-link emits the
    # guard tables; needs the same flag on both lines (below). Adds no import.
    /guard:cf
    # clang-cl 19 ignores /Zc:preprocessor; do not promote that driver note to an error.
    -Wno-unused-command-line-argument
    $<$<BOOL:${PVDKIT_COVERAGE}>:/Od>
    $<$<BOOL:${PVDKIT_COVERAGE}>:/Zi>
    $<$<BOOL:${PVDKIT_COVERAGE}>:-fprofile-instr-generate>
    $<$<BOOL:${PVDKIT_COVERAGE}>:-fcoverage-mapping>
    # Profile counters are plain adds by default, so the increments of eight threads decoding
    # through the instrumented DLL at once (the leak tests) lose each other and a branch taken on
    # every thread can read as never taken: the coverage-x86 gate once reported 99.657 % branches
    # with a switch default derived as entry minus the sum of the cases = 0. Atomic updates (lock
    # add) make the counts exact; a probe of 8 threads x 2 M calls recorded 1.4-3.0 M of 8 M
    # increments without this flag and exactly 8 M with it, on both architectures.
    $<$<BOOL:${PVDKIT_COVERAGE}>:/clang:-fprofile-update=atomic>
    $<$<BOOL:${PVDKIT_ASAN}>:/Od>
    $<$<BOOL:${PVDKIT_ASAN}>:/Zi>
    $<$<BOOL:${PVDKIT_ASAN}>:-fsanitize=address>)
target_link_options(
  pvdkit_options
  INTERFACE
    /guard:cf
    $<$<CONFIG:Release>:/DEBUG:NONE>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<BOOL:${PVDKIT_COVERAGE}>:-fprofile-instr-generate>
    $<$<BOOL:${PVDKIT_ASAN}>:/DEBUG>)

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

if(PVDKIT_ASAN)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "PVDKIT_ASAN is x64 only: the asan preset links the x86_64 runtime of the x64-hosted LLVM")
  endif()
  # CMake links with lld-link directly, so the runtime libraries clang-cl's own driver would add
  # must be named here (clang/lib/Driver/ToolChains/MSVC.cpp, static CRT): an executable gets the
  # runtime itself, clang_rt.asan and clang_rt.asan_cxx as whole archives so every interface
  # symbol an instrumented DLL may need is present and exported; a DLL gets clang_rt.asan_dll_thunk,
  # which forwards to the executable's runtime (and imports nothing new - the thunk resolves it
  # through GetProcAddress). The type of the target being linked selects the set.
  get_filename_component(_pvdkit_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  foreach(_pvdkit_asan_lib asan asan_cxx asan_dll_thunk)
    file(GLOB _pvdkit_asan_${_pvdkit_asan_lib}
         "${_pvdkit_llvm_bin}/../lib/clang/*/lib/windows/clang_rt.${_pvdkit_asan_lib}-x86_64.lib")
    if(NOT _pvdkit_asan_${_pvdkit_asan_lib})
      message(FATAL_ERROR "clang_rt.${_pvdkit_asan_lib}-x86_64.lib was not found under ${_pvdkit_llvm_bin}/../lib/clang")
    endif()
    list(GET _pvdkit_asan_${_pvdkit_asan_lib} 0 _pvdkit_asan_${_pvdkit_asan_lib})
  endforeach()
  target_link_options(
    pvdkit_options
    INTERFACE
      "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:/wholearchive:${_pvdkit_asan_asan}>"
      "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:/wholearchive:${_pvdkit_asan_asan_cxx}>")
  target_link_libraries(
    pvdkit_options
    INTERFACE "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${_pvdkit_asan_asan_dll_thunk}>")
  unset(_pvdkit_llvm_bin)
  unset(_pvdkit_asan_lib)
  unset(_pvdkit_asan_asan)
  unset(_pvdkit_asan_asan_cxx)
  unset(_pvdkit_asan_asan_dll_thunk)
  # One translation unit stays uninstrumented: tests/TestMain.cpp explains why (a clang-cl 19 ASan
  # limitation with `throw;` inside a catch handler, which only doctest's own code does).
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
