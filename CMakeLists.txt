cmake_minimum_required(VERSION 3.28)

project(avifpvd VERSION 1.0.0 LANGUAGES CXX)

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

find_package(libavif CONFIG REQUIRED)

include(CTest)

add_subdirectory(src)
if(BUILD_TESTING)
  find_package(doctest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
