#pragma once

#include <memory>

#include "core/IDecoder.hpp"
#include "core/IFileSource.hpp"
#include "pvd/Plugin.hpp"

namespace avifpvd::core {

class AvifPlugin final : public pvd::IPlugin {
public:
  AvifPlugin(IFileSource &fileSource, IDecoderFactory &decoderFactory,
             DecoderOptions options, pvd::PluginInfo pluginInfo);

  AvifPlugin(const AvifPlugin &) = delete;
  AvifPlugin &operator=(const AvifPlugin &) = delete;
  AvifPlugin(AvifPlugin &&) = delete;
  AvifPlugin &operator=(AvifPlugin &&) = delete;

  [[nodiscard]] const pvd::PluginInfo &info() const override;
  [[nodiscard]] Result<std::unique_ptr<pvd::IFileSession>>
  open(const pvd::OpenRequest &request) override;

private:
  IFileSource &fileSource_;
  IDecoderFactory &decoderFactory_;
  DecoderOptions options_;
  pvd::PluginInfo pluginInfo_;
};

} // namespace avifpvd::core
