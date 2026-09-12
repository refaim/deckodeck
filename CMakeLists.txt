cmake_minimum_required(VERSION 3.28)

# RC: src/pvd/AVIF.rc (VERSIONINFO), compiled by llvm-rc from the chainload toolchain.
project(avifpvd VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AVIFPVD_COVERAGE "Instrument AVIF.pvd targets for llvm-cov" OFF)

add_library(avifpvd_options INTERFACE)
target_compile_options(
  avifpvd_options
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
    $<$<BOOL:${AVIFPVD_COVERAGE}>:/Od>
    $<$<BOOL:${AVIFPVD_COVERAGE}>:/Zi>
    $<$<BOOL:${AVIFPVD_COVERAGE}>:-fprofile-instr-generate>
    $<$<BOOL:${AVIFPVD_COVERAGE}>:-fcoverage-mapping>)
target_link_options(
  avifpvd_options
  INTERFACE
    $<$<CONFIG:Release>:/DEBUG:NONE>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<BOOL:${AVIFPVD_COVERAGE}>:-fprofile-instr-generate>)

if(AVIFPVD_COVERAGE AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  # Every instrumented object names the profile runtime in a /DEFAULTLIB directive, and lld-link
  # looks for it in its own resource directory. The x64-hosted LLVM that cross-compiles the x86
  # build ships only clang_rt.profile-x86_64.lib there, so clang falls back to the arch-less name
  # clang_rt.profile.lib, which exists nowhere. The i386 runtime of the same LLVM version comes
  # with the x86-hosted LLVM of the same Build Tools (VC/Tools/Llvm/lib/clang/<v>/lib/windows):
  # drop the unresolvable default and link that file explicitly. (x64 needs nothing: its runtime
  # is found next to lld-link.)
  get_filename_component(_avifpvd_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  file(GLOB _avifpvd_profile_runtime
       "${_avifpvd_llvm_bin}/../../lib/clang/*/lib/windows/clang_rt.profile-i386.lib")
  if(NOT _avifpvd_profile_runtime)
    message(FATAL_ERROR "clang_rt.profile-i386.lib was not found under ${_avifpvd_llvm_bin}/../../lib/clang")
  endif()
  list(GET _avifpvd_profile_runtime 0 _avifpvd_profile_runtime)
  target_link_options(avifpvd_options INTERFACE /NODEFAULTLIB:clang_rt.profile.lib)
  target_link_libraries(avifpvd_options INTERFACE "${_avifpvd_profile_runtime}")
  unset(_avifpvd_llvm_bin)
  unset(_avifpvd_profile_runtime)
endif()

find_package(libavif CONFIG REQUIRED)

include(CTest)

add_subdirectory(src)
if(BUILD_TESTING)
  find_package(doctest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
