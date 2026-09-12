// Composition of the production plugin: the only place where the Win32 and libavif adapters meet
// the core (ARCHITECTURE §3.7). `Exports.cpp` calls `makePlugin()` without knowing any of them.
#include "pvd/PluginFactory.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include "adapters/avif/Decoder.hpp"
#include "adapters/win/FileSource.hpp"
#include "core/AvifPlugin.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Plugin.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/Types.hpp"

namespace avifpvd::pvd {
namespace {

constexpr std::uint64_t kMaxPixels = std::uint64_t{16384} * 16384;
constexpr std::uint32_t kMaxDimension = 32768;

core::DecoderOptions defaultOptions() {
  return core::DecoderOptions{std::max(1U, std::thread::hardware_concurrency()), false, kMaxPixels,
                              kMaxDimension};
}

PluginInfo defaultInfo() {
  return PluginInfo{kPluginPriority, std::string{kPluginName}, std::string{kPluginVersion},
                    "AVIF decoder: " + avif::libraryVersions() + "; static build"};
}

// Owns the adapters and the core plugin in dependency order: `AvifPlugin` holds references to the
// two members declared before it, so they are constructed first and destroyed last.
class DefaultPlugin final : public IPlugin {
 public:
  DefaultPlugin() : plugin_{fileSource_, decoderFactory_, defaultOptions(), defaultInfo()} {}

  [[nodiscard]] const PluginInfo& info() const override { return plugin_.info(); }

  [[nodiscard]] core::Result<std::unique_ptr<IFileSession>> open(
      const OpenRequest& request) override {
    return plugin_.open(request);
  }

 private:
  win::FileSource fileSource_;
  avif::DecoderFactory decoderFactory_;
  core::AvifPlugin plugin_;
};

}  // namespace

std::unique_ptr<IPlugin> makePlugin() { return std::make_unique<DefaultPlugin>(); }

}  // namespace avifpvd::pvd
