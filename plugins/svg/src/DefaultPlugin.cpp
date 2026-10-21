// Composition root of SVG.pvd: Win32, resvg, the SVG describer and the shared codec core meet here.
#include "pvd/PluginFactory.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include "adapters/resvg/Decoder.hpp"
#include "adapters/win/FileSource.hpp"
#include "core/CodecPlugin.hpp"
#include "core/Describe.hpp"
#include "core/IDecoder.hpp"
#include "core/colour/Pipeline.hpp"
#include "pvd/Plugin.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::pvd
{
    namespace
    {

        constexpr std::uint64_t kMaxPixels = std::uint64_t{8192} * 8192;
        constexpr std::uint32_t kMaxDimension = 16'384;

        core::DecoderOptions defaultOptions()
        {
            return {1, false, kMaxPixels, kMaxDimension, false, true};
        }

        PluginInfo defaultInfo()
        {
            return {kPluginIdentity.priority, std::string{kPluginIdentity.name}, std::string{kPluginIdentity.version},
                    "SVG/SVGZ renderer: resvg 0.48.1; static build"};
        }

        class DefaultPlugin final : public IPlugin
        {
          public:
            explicit DefaultPlugin(const core::DecoderOptions &options)
                : decoderFactory_{fontState_},
                  plugin_{fileSource_, decoderFactory_, describer_, tables_, options, defaultInfo()}
            {
            }

            [[nodiscard]] const PluginInfo &info() const override
            {
                return plugin_.info();
            }

            [[nodiscard]] core::Result<std::unique_ptr<IFileSession>> open(const OpenRequest &request) override
            {
                return plugin_.open(request);
            }

          private:
            win::FileSource fileSource_;
            svg::SvgState fontState_;
            svg::DecoderFactory decoderFactory_;
            svg::Describer describer_;
            core::colour::ColourTables tables_;
            core::CodecPlugin plugin_;
        };

    } // namespace

    std::unique_ptr<IPlugin> makePlugin()
    {
        return makePlugin(defaultOptions());
    }

    std::unique_ptr<IPlugin> makePlugin(const core::DecoderOptions &options)
    {
        return std::make_unique<DefaultPlugin>(options);
    }

} // namespace pvdkit::pvd
