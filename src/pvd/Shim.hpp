#pragma once

#include "pvd/Plugin.hpp"
#include "pvd/PluginIdentity.hpp"
#include "pvd/PvdApi.hpp"

namespace pvdkit::pvd {

/// Fills `output` with the constant `identity` (priority, name, version, empty comments). Used
/// both when no plugin is alive and as the fallback that `Shim::pluginInfo` writes before it asks
/// the plugin, so a throwing `info()` can never leave the host with garbage. A null `output` is
/// ignored.
void fillDefaultPluginInfo(pvdInfoPlugin* output, const PluginIdentity& identity) noexcept;

class Shim final {
 public:
  /// `identity` is the plugin's constant identity (its generated `kPluginIdentity`), the fallback
  /// `pluginInfo` writes before consulting `plugin.info()`.
  Shim(IPlugin& plugin, PluginIdentity identity) noexcept;

  Shim(const Shim&) = delete;
  Shim& operator=(const Shim&) = delete;
  Shim(Shim&&) = delete;
  Shim& operator=(Shim&&) = delete;

  [[nodiscard]] UINT32 init() noexcept;
  void exit() noexcept;
  void pluginInfo(pvdInfoPlugin* output) noexcept;
  [[nodiscard]] BOOL fileOpen(const char* fileName, INT64 fileSize, const BYTE* head,
                              UINT32 headSize, pvdInfoImage* output, void** context) noexcept;
  [[nodiscard]] BOOL pageInfo(void* context, UINT32 page, pvdInfoPage* output) noexcept;
  [[nodiscard]] BOOL pageDecode(void* context, UINT32 page, pvdInfoDecode* output,
                                pvdDecodeCallback callback, void* callbackContext) noexcept;
  void pageFree(void* context, pvdInfoDecode* decoded) noexcept;
  void fileClose(void* context) noexcept;

 private:
  IPlugin& plugin_;
  PluginIdentity identity_;
};

}  // namespace pvdkit::pvd
