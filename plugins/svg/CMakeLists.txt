# SVG.pvd: SVG/SVGZ renderer over the upstream resvg C API, linked statically with YY-Thunks.
include(FetchContent)

set(PVDKIT_RESVG_VERSION "0.48.1" CACHE STRING "resvg version bundled by SVG.pvd")
set(PVDKIT_RESVG_COMMIT "68b14c4c3bccdb60344c777406486b54c36ec1a4" CACHE STRING "resvg source commit")
set(PVDKIT_RESVG_URL
    "https://github.com/linebender/resvg/archive/refs/tags/v${PVDKIT_RESVG_VERSION}.tar.gz"
    CACHE STRING "resvg source archive")
set(PVDKIT_RESVG_SHA256 "40dafea6b4b9d01e9d28b6d49f1e912daf3e9055676ad9179a5a2db6e7386945"
    CACHE STRING "SHA-256 of the pinned resvg source archive")

FetchContent_Declare(
  svg_resvg_source
  URL "${PVDKIT_RESVG_URL}"
  URL_HASH "SHA256=${PVDKIT_RESVG_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(svg_resvg_source)

# YY-Thunks 1.2.3 produces the Windows 7 static-library overlays used for Rust's imports.
set(PVDKIT_YY_THUNKS_VERSION "1.2.3" CACHE STRING "YY-Thunks release bundled by SVG.pvd")
set(PVDKIT_YY_THUNKS_URL
    "https://github.com/Chuyu-Team/YY-Thunks/releases/download/v${PVDKIT_YY_THUNKS_VERSION}/YY-Thunks-Lib.zip"
    CACHE STRING "YY-Thunks library archive")
set(PVDKIT_YY_THUNKS_SHA256 "8d2417cea1914a678f766c73fd458bb85dfd6057878f8d1d87ddcfc370a42ef9"
    CACHE STRING "SHA-256 of the pinned YY-Thunks library archive")

FetchContent_Declare(
  svg_yy_thunks
  URL "${PVDKIT_YY_THUNKS_URL}"
  URL_HASH "SHA256=${PVDKIT_YY_THUNKS_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(svg_yy_thunks)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(SVG_RUST_TARGET "x86_64-pc-windows-msvc")
  set(SVG_YY_ARCH "x64")
  set(SVG_YY_ENTRY_ALIAS "/alternatename:YY_ThunksOriginalDllMainCRTStartup=_DllMainCRTStartup")
  set(SVG_YY_SUBSYSTEM "/SUBSYSTEM:WINDOWS,6.1")
else()
  set(SVG_RUST_TARGET "i686-pc-windows-msvc")
  set(SVG_YY_ARCH "x86")
  set(SVG_YY_ENTRY_ALIAS "/alternatename:_YY_ThunksOriginalDllMainCRTStartup@12=__DllMainCRTStartup@12")
  set(SVG_YY_SUBSYSTEM "/SUBSYSTEM:WINDOWS,6.01")
endif()

set(SVG_YY_ROOT "${svg_yy_thunks_SOURCE_DIR}/Lib/6.1.7600.0/${SVG_YY_ARCH}")
set(SVG_YY_LICENSE "${svg_yy_thunks_SOURCE_DIR}/LICENSE")
# The official Win7 overlay has an ntdll archive only for x64. x86 resolves ntdll from the
# native Windows SDK import library, which is the intended source there.
set(SVG_YY_REQUIRED_LIBRARIES kernel32 userenv ws2_32 synchronization)
if(SVG_YY_ARCH STREQUAL "x64")
  list(APPEND SVG_YY_REQUIRED_LIBRARIES ntdll)
endif()
foreach(library IN LISTS SVG_YY_REQUIRED_LIBRARIES)
  if(NOT EXISTS "${SVG_YY_ROOT}/${library}.Lib" AND NOT EXISTS "${SVG_YY_ROOT}/${library}.lib")
    message(FATAL_ERROR "YY-Thunks ${SVG_YY_THUNKS_VERSION} does not provide ${library} for ${SVG_YY_ARCH}")
  endif()
endforeach()

set(PVDKIT_RESVG_RUST_RELEASE "1.99.1")
set(PVDKIT_RESVG_RUST_TOOLCHAIN "1.99.1" CACHE STRING
    "Rust toolchain name or existing absolute toolchain directory used for resvg")
find_program(PVDKIT_RUSTUP_EXECUTABLE NAMES rustup REQUIRED)

if(IS_ABSOLUTE "${PVDKIT_RESVG_RUST_TOOLCHAIN}")
  if(NOT IS_DIRECTORY "${PVDKIT_RESVG_RUST_TOOLCHAIN}")
    message(FATAL_ERROR
            "PVDKIT_RESVG_RUST_TOOLCHAIN='${PVDKIT_RESVG_RUST_TOOLCHAIN}' is not an existing directory")
  endif()
  set(_pvdkit_rustc "${PVDKIT_RESVG_RUST_TOOLCHAIN}/bin/rustc.exe")
  if(NOT EXISTS "${_pvdkit_rustc}" OR IS_DIRECTORY "${_pvdkit_rustc}")
    message(FATAL_ERROR
            "PVDKIT_RESVG_RUST_TOOLCHAIN='${PVDKIT_RESVG_RUST_TOOLCHAIN}' has no bin/rustc.exe")
  endif()
elseif(PVDKIT_RESVG_RUST_TOOLCHAIN STREQUAL "")
  message(FATAL_ERROR "PVDKIT_RESVG_RUST_TOOLCHAIN must name a toolchain or an absolute directory")
endif()

# Validate the compiler the Cargo command will actually use. A caller-provided cache value is
# not evidence, and a named toolchain must be probed through rustup before any Cargo invocation.
execute_process(
  COMMAND "${PVDKIT_RUSTUP_EXECUTABLE}" run "${PVDKIT_RESVG_RUST_TOOLCHAIN}" rustc -vV
  OUTPUT_VARIABLE _pvdkit_rustc_report
  ERROR_VARIABLE _pvdkit_rustc_stderr
  RESULT_VARIABLE _pvdkit_rustc_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _pvdkit_rustc_result EQUAL 0)
  message(FATAL_ERROR
          "rustup run ${PVDKIT_RESVG_RUST_TOOLCHAIN} rustc -vV failed with exit code "
          "${_pvdkit_rustc_result}: ${_pvdkit_rustc_stderr}")
endif()
if(NOT _pvdkit_rustc_report MATCHES "(^|\n)release: ([^\r\n]+)")
  message(FATAL_ERROR "Selected rustc -vV has no release line: ${_pvdkit_rustc_report}")
endif()
set(_pvdkit_rustc_release "${CMAKE_MATCH_2}")
if(NOT _pvdkit_rustc_release STREQUAL PVDKIT_RESVG_RUST_RELEASE)
  message(FATAL_ERROR
          "Selected Rust toolchain '${PVDKIT_RESVG_RUST_TOOLCHAIN}' reports release "
          "${_pvdkit_rustc_release}; resvg requires ${PVDKIT_RESVG_RUST_RELEASE}")
endif()

execute_process(
  COMMAND "${PVDKIT_RUSTUP_EXECUTABLE}" run "${PVDKIT_RESVG_RUST_TOOLCHAIN}" rustc --print sysroot
  OUTPUT_VARIABLE _pvdkit_rustc_sysroot
  ERROR_VARIABLE _pvdkit_rustc_sysroot_stderr
  RESULT_VARIABLE _pvdkit_rustc_sysroot_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _pvdkit_rustc_sysroot_result EQUAL 0)
  message(FATAL_ERROR
          "rustup run ${PVDKIT_RESVG_RUST_TOOLCHAIN} rustc --print sysroot failed with exit code "
          "${_pvdkit_rustc_sysroot_result}: ${_pvdkit_rustc_sysroot_stderr}")
endif()
set(_pvdkit_rustc "${_pvdkit_rustc_sysroot}/bin/rustc.exe")
if(NOT EXISTS "${_pvdkit_rustc}" OR IS_DIRECTORY "${_pvdkit_rustc}")
  message(FATAL_ERROR "Selected rustc sysroot '${_pvdkit_rustc_sysroot}' has no bin/rustc.exe")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pvdkit_rustc}")

set(PVDKIT_RESVG_CARGO_TARGET_DIR "${svg_resvg_source_SOURCE_DIR}/target"
    CACHE PATH "Cargo target directory holding the cached resvg static library")
# Upstream resvg 0.48.1 declares [profile.production] as a release-derived LTO profile; Cargo
# names the output directory after the profile, so the command and the archive path share it.
set(SVG_RESVG_CARGO_PROFILE "production")
set(SVG_RESVG_LIBRARY
    "${PVDKIT_RESVG_CARGO_TARGET_DIR}/${SVG_RUST_TARGET}/${SVG_RESVG_CARGO_PROFILE}/resvg.lib")
add_custom_command(
  OUTPUT "${SVG_RESVG_LIBRARY}"
  COMMAND
    "${CMAKE_COMMAND}" -E env "RUSTFLAGS=-C target-feature=+crt-static"
    "${PVDKIT_RUSTUP_EXECUTABLE}" run "${PVDKIT_RESVG_RUST_TOOLCHAIN}"
    cargo rustc -p resvg-capi --manifest-path "${svg_resvg_source_SOURCE_DIR}/Cargo.toml"
    --lib --profile ${SVG_RESVG_CARGO_PROFILE} --locked --target "${SVG_RUST_TARGET}"
    --target-dir "${PVDKIT_RESVG_CARGO_TARGET_DIR}"
    --crate-type staticlib
  DEPENDS
    "${svg_resvg_source_SOURCE_DIR}/Cargo.toml"
    "${svg_resvg_source_SOURCE_DIR}/Cargo.lock"
    "${svg_resvg_source_SOURCE_DIR}/crates/c-api/Cargo.toml"
    "${svg_resvg_source_SOURCE_DIR}/crates/c-api/lib.rs"
    "${_pvdkit_rustc}"
  WORKING_DIRECTORY "${svg_resvg_source_SOURCE_DIR}"
  COMMENT "Building resvg ${PVDKIT_RESVG_VERSION} C API static library for ${SVG_RUST_TARGET}"
  VERBATIM)
add_custom_target(svg_resvg_build DEPENDS "${SVG_RESVG_LIBRARY}")
add_library(svg_resvg STATIC IMPORTED GLOBAL)
set_target_properties(svg_resvg PROPERTIES IMPORTED_LOCATION "${SVG_RESVG_LIBRARY}")
add_dependencies(svg_resvg svg_resvg_build)
target_include_directories(svg_resvg INTERFACE "${svg_resvg_source_SOURCE_DIR}/crates/c-api")

pvdkit_plugin_identity(
  svg
  NAME SVG
  VERSION 1.0.0
  PRIORITY 10
  DESCRIPTION "SVG/SVGZ renderer plugin for PictureView (Far Manager)"
  COMMENTS "SVG/SVGZ renderer: resvg ${PVDKIT_RESVG_VERSION}; static build")

file(GLOB_RECURSE SVG_CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp" "src/core/*.hpp")
add_library(svg_core STATIC ${SVG_CORE_SOURCES})
target_include_directories(svg_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(svg_core PUBLIC pvdkit_core pvdkit_options)

file(GLOB_RECURSE SVG_ADAPTER_SOURCES CONFIGURE_DEPENDS "src/adapters/*.cpp" "src/adapters/*.hpp")
add_library(svg_adapter STATIC ${SVG_ADAPTER_SOURCES})
target_include_directories(svg_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(svg_adapter PUBLIC svg_core pvdkit_core pvdkit_options PRIVATE svg_resvg pvdkit_win)
add_dependencies(svg_adapter svg_resvg)

add_library(svg_composition STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/DefaultPlugin.cpp")
target_link_libraries(
  svg_composition
  PUBLIC svg_identity svg_adapter svg_core pvdkit_win pvdkit_pvd pvdkit_options)

# Rust's import set is resolved through the official YY-Thunks Win7 libraries before the native
# SDK archives. The native archives precede resvg.lib so its unresolved references find them.
target_link_directories(svg_composition BEFORE PRIVATE "${SVG_YY_ROOT}")
target_link_libraries(
  svg_composition
  PRIVATE kernel32
          ntdll
          userenv
          ws2_32
          dbghelp
          synchronization
          svg_resvg)

pvdkit_add_plugin(
  svg
  LINK svg_composition
  IMPORTS KERNEL32.dll ntdll.dll ole32.dll
  LICENSES_COPYONLY
    "third_party/resvg/LICENSES-resvg.txt" "resvg 0.48.1 (MIT), YY-Thunks 1.2.3 (MIT)"
  EXTRA_DOCUMENTS
    "third_party/resvg/RUST-COPYRIGHT-library.html"
    "third_party/resvg/RUST-COPYRIGHT.html")


target_link_options(
  svg_plugin
  PRIVATE "/ENTRY:DllMainCRTStartupForYY_Thunks" "${SVG_YY_ENTRY_ALIAS}" "${SVG_YY_SUBSYSTEM}" "/OPT:REF"
          # lld-link's COFF tail merge folds ASan global redzones; disable it only for the sanitizer link.
          $<$<BOOL:${PVDKIT_ASAN}>:/OPT:NOLLDTAILMERGE>)
target_link_directories(svg_plugin BEFORE PRIVATE "${SVG_YY_ROOT}")

if(BUILD_TESTING)
  set(SVG_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_subdirectory(tests)
endif()
