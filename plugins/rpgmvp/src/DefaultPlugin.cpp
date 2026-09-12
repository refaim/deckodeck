// Composition root of RPGMVP.pvd: the only place where Win32, libspng, the format describer and
// the shared codec core meet.
#include "pvd/PluginFactory.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include "adapters/spng/Decoder.hpp"
#include "adapters/win/FileSource.hpp"
#include "core/CodecPlugin.hpp"
#include "core/Describe.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Plugin.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::pvd
{
    namespace
    {

        constexpr std::uint64_t kMaxPixels = std::uint64_t{16'384} * 16'384;
        constexpr std::uint32_t kMaxDimension = 32'768;

        core::DecoderOptions defaultOptions()
        {
            return {std::max(1U, std::thread::hardware_concurrency()), false, kMaxPixels, kMaxDimension};
        }

        PluginInfo defaultInfo()
        {
            return {kPluginIdentity.priority, std::string{kPluginIdentity.name}, std::string{kPluginIdentity.version},
                    "RPG Maker MV/MZ encrypted PNG decoder: " + rpgmvp::libraryVersions() + "; static build"};
        }

        class DefaultPlugin final : public IPlugin
        {
          public:
            DefaultPlugin() : plugin_{fileSource_, decoderFactory_, describer_, defaultOptions(), defaultInfo()}
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
            rpgmvp::DecoderFactory decoderFactory_;
            rpgmvp::Describer describer_;
            core::CodecPlugin plugin_;
        };

    } // namespace

    std::unique_ptr<IPlugin> makePlugin()
    {
        return std::make_unique<DefaultPlugin>();
    }

} // namespace pvdkit::pvd
