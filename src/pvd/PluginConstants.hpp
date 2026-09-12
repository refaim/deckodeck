#pragma once

#include <cstdint>
#include <string_view>

#include "pvd/Version.hpp"

namespace avifpvd::pvd {

/// Identity of this decoder as reported to PictureView. `Shim::pluginInfo` and the `pvdPluginInfo`
/// export fall back to these values when no plugin is alive; `DefaultPlugin` builds its
/// `PluginInfo` from the same constants so the two can never drift apart.
inline constexpr std::uint32_t kPluginPriority = 10;
inline constexpr std::string_view kPluginName = "AVIF";
inline constexpr std::string_view kPluginVersion = "1.0.0";

// The host receives `data()` of these views as C strings, so they must stay literal-backed and
// therefore null-terminated; the asserts read the literal's terminator to pin that.
static_assert(kPluginName.data()[kPluginName.size()] == '\0');
static_assert(kPluginVersion.data()[kPluginVersion.size()] == '\0');

// The CMake project version (project(VERSION) in CMakeLists.txt, generated into pvd/Version.hpp)
// is the single source of the version: it feeds the VERSIONINFO resource, and the constant the
// host sees must say the same, or the build stops here.
static_assert(kPluginVersion == AVIFPVD_VERSION_STRING,
              "kPluginVersion must match the CMake project version (CMakeLists.txt project(VERSION))");

}  // namespace avifpvd::pvd
