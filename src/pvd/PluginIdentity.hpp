#pragma once

#include <cstdint>
#include <string_view>

namespace pvdkit::pvd {

/// The constant identity of one plugin as reported to PictureView: what `pvdPluginInfo` answers
/// before `pvdInit` and what `Shim::pluginInfo` writes before it consults `IPlugin::info()`, so
/// a throwing `info()` can never leave the host with garbage. The shared marshalling layer only
/// forwards a value it is given; every plugin's own value, `kPluginIdentity`, lives in its
/// generated `pvd/PluginConstants.hpp` (see `cmake/pvdkit-plugin.cmake`), which is the one source
/// of the plugin's name, version and priority for the C++ side and the VERSIONINFO resource alike.
struct PluginIdentity {
  std::uint32_t priority;
  /// The host receives `data()` of these views as C strings, so they must be literal-backed and
  /// therefore null-terminated; the generated header `static_assert`s exactly that.
  std::string_view name;
  std::string_view version;
};

}  // namespace pvdkit::pvd
