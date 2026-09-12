// Composition root of AVIF.pvd: the only place where the Win32 and libavif adapters, the AVIF
// describer and the shared core meet (ARCHITECTURE §3.7). `Exports.cpp` calls `makePlugin()`
// without knowing any of them.
#include "pvd/PluginFactory.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include "adapters/avif/Decoder.hpp"
#include "adapters/win/FileSource.hpp"
#include "core/CodecPlugin.hpp"
#include "core/Describe.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Plugin.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::pvd {
namespace {

constexpr std::uint64_t kMaxPixels = std::uint64_t{16384} * 16384;
constexpr std::uint32_t kMaxDimension = 32768;

core::DecoderOptions defaultOptions() {
  return core::DecoderOptions{std::max(1U, std::thread::hardware_concurrency()), false, kMaxPixels,
                              kMaxDimension};
}

// Identity from the generated pvd/PluginConstants.hpp (the same values the VERSIONINFO resource
// carries); the comments repeat the resource's text from the libraries actually linked in, and
// the e2e version test pins the two equal.
PluginInfo defaultInfo() {
  return PluginInfo{kPluginIdentity.priority, std::string{kPluginIdentity.name},
                    std::string{kPluginIdentity.version},
                    "AVIF decoder: " + avif::libraryVersions() + "; static build"};
}

// Owns the adapters, the describer and the core plugin in dependency order: `CodecPlugin` holds
// references to the three members declared before it, so they are constructed first and
// destroyed last.
class DefaultPlugin final : public IPlugin {
 public:
  DefaultPlugin()
      : plugin_{fileSource_, decoderFactory_, describer_, defaultOptions(), defaultInfo()} {}

  [[nodiscard]] const PluginInfo& info() const override { return plugin_.info(); }

  [[nodiscard]] core::Result<std::unique_ptr<IFileSession>> open(
      const OpenRequest& request) override {
    return plugin_.open(request);
  }

 private:
  win::FileSource fileSource_;
  avif::DecoderFactory decoderFactory_;
  avif::Describer describer_;
  core::CodecPlugin plugin_;
};

}  // namespace

std::unique_ptr<IPlugin> makePlugin() { return std::make_unique<DefaultPlugin>(); }

}  // namespace pvdkit::pvd
