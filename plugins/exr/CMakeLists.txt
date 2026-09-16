# EXR.pvd: OpenEXR decoder over OpenEXRCore (the library's C API) with Imath, libdeflate and
# OpenJPH, all linked statically.
find_package(OpenEXR CONFIG REQUIRED)
find_package(Imath CONFIG REQUIRED)
find_package(libdeflate CONFIG REQUIRED)
find_package(openjph CONFIG REQUIRED)

# The pvdPluginInfo comments repeat what DefaultPlugin.cpp reports at run time ("OpenEXR decoder:
# <exr::libraryVersions()>; static build"); the e2e version test pins the two strings equal. Every
# version is what its find_package found, i.e. the port vcpkg installed.
if(NOT OpenEXR_VERSION OR NOT Imath_VERSION OR NOT libdeflate_VERSION OR NOT openjph_VERSION)
  message(FATAL_ERROR "Library versions for the VERSIONINFO comments were not found: "
                      "OpenEXR '${OpenEXR_VERSION}', Imath '${Imath_VERSION}', "
                      "libdeflate '${libdeflate_VERSION}', OpenJPH '${openjph_VERSION}'")
endif()

# The one source of the plugin's identity (name, version, priority): the generated
# pvd/PluginConstants.hpp feeds Exports.cpp, DefaultPlugin.cpp, the VERSIONINFO resource and the
# package manifest.
pvdkit_plugin_identity(
  exr
  NAME EXR
  VERSION 1.0.0
  PRIORITY 10
  DESCRIPTION "OpenEXR decoder plugin for PictureView (Far Manager)"
  COMMENTS "OpenEXR decoder: OpenEXR ${OpenEXR_VERSION}, Imath ${Imath_VERSION}, libdeflate ${libdeflate_VERSION}, OpenJPH ${openjph_VERSION}; static build")

# EXR-specific decisions without the library: channel and part selection, window layout and
# compositing, exposure and the tone-mapping peak, chromaticity matching, the describer.
file(GLOB_RECURSE EXR_CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp" "src/core/*.hpp")
add_library(exr_core STATIC ${EXR_CORE_SOURCES})
target_include_directories(exr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(exr_core PUBLIC pvdkit_core pvdkit_options)

# The OpenEXR adapter: OpenEXRCore's C API for everything (context over the file bytes, header,
# chunk decoding); from the C++ library only four Imf::RgbaYca free functions (the chroma
# reconstruction of luminance-chroma files: reconstructChromaHoriz/Vert, YCAtoRGBA,
# fixSaturation - arithmetic over half rows that throws nothing; computeYw, which throws on a
# degenerate chromaticities set, is not used: the reconstruction weights are exr::luminanceWeights,
# computeYw's arithmetic spelled out over a set the core has validated (LuminanceChroma.cpp), and
# the histogram weights come from the shared Primaries::luminanceCoefficients - DESIGN.md 1 and 5)
# - the linker pulls ImfRgbaYca.obj and nothing else of OpenEXR-3_4.lib, so the DLL carries no
# Iex exception machinery and no IlmThread pool (plugins/exr/DESIGN.md, "Library").
# Imath's half-to-float lookup table is not used either (IMATH_HALF_NO_LOOKUP_TABLE, and the
# ports/imath overlay builds Imath and OpenEXR without it): the object defining the table,
# half.cpp.obj, drags in <iostream> locale initialisers and thus the CRT's thread_safe_statics.obj,
# the very object that imports the synch API set on MSVC >= 14.50 (rule 13). libdeflate and
# OpenJPH are reached only through the Core; their targets supply the version headers the
# comments quote.
file(GLOB_RECURSE EXR_ADAPTER_SOURCES CONFIGURE_DEPENDS "src/adapters/*.cpp" "src/adapters/*.hpp")
add_library(exr_adapter STATIC ${EXR_ADAPTER_SOURCES})
target_compile_definitions(exr_adapter PRIVATE IMATH_HALF_NO_LOOKUP_TABLE)
target_include_directories(exr_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
  exr_adapter
  PUBLIC exr_core pvdkit_core pvdkit_options
         OpenEXR::OpenEXRCore OpenEXR::OpenEXR Imath::Imath libdeflate::libdeflate_static openjph)

# The composition root (pvd::makePlugin over win::FileSource, exr::DecoderFactory, exr::Describer
# and core::CodecPlugin), a library so the plugin's tests can call it too.
add_library(exr_composition STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/DefaultPlugin.cpp")
target_link_libraries(exr_composition PUBLIC exr_identity exr_adapter exr_core pvdkit_win pvdkit_pvd pvdkit_options)

pvdkit_add_plugin(
  exr
  LINK exr_composition
  LICENSES openexr "OpenEXR (BSD-3-Clause)" imath "Imath (BSD-3-Clause)" libdeflate "libdeflate (MIT)"
           openjph "OpenJPH (BSD-2-Clause)")

if(BUILD_TESTING)
  set(EXR_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_subdirectory(tests)
endif()
