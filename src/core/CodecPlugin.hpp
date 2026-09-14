#pragma once

#include <memory>

#include "core/IDecoder.hpp"
#include "core/IFileSource.hpp"
#include "core/IImageDescriber.hpp"
#include "pvd/Plugin.hpp"

namespace pvdkit::core
{
    namespace colour
    {
        class SrgbOutputTables;
    }

    /// The format-neutral `pvd::IPlugin`: signature check through the decoder factory, file or memory
    /// mode, decoder creation and session construction. Everything format-specific is injected: the
    /// factory (adapter over the codec library), the describer (the plugin's host-facing words) and the
    /// plugin-wide sRGB output tables that every session's colour presentation borrows (built once per
    /// plugin instance by the composition root, ARCHITECTURE section 7).
    class CodecPlugin final : public pvd::IPlugin
    {
      public:
        CodecPlugin(IFileSource &fileSource, IDecoderFactory &decoderFactory, const IImageDescriber &describer,
                    const colour::SrgbOutputTables &outputTables, const DecoderOptions &options,
                    pvd::PluginInfo pluginInfo);

        CodecPlugin(const CodecPlugin &) = delete;
        CodecPlugin &operator=(const CodecPlugin &) = delete;
        CodecPlugin(CodecPlugin &&) = delete;
        CodecPlugin &operator=(CodecPlugin &&) = delete;

        [[nodiscard]] const pvd::PluginInfo &info() const override;
        [[nodiscard]] Result<std::unique_ptr<pvd::IFileSession>> open(const pvd::OpenRequest &request) override;

      private:
        IFileSource &fileSource_;
        IDecoderFactory &decoderFactory_;
        const IImageDescriber &describer_;
        const colour::SrgbOutputTables &outputTables_;
        DecoderOptions options_;
        pvd::PluginInfo pluginInfo_;
    };

} // namespace pvdkit::core
