// Composition root of EXR.pvd: the only place where the Win32 and OpenEXR adapters, the EXR
// describer and the shared core meet (ARCHITECTURE section 3.7). `Exports.cpp` calls `makePlugin()`
// without knowing any of them.
#include "pvd/PluginFactory.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include "adapters/exr/Context.hpp"
#include "adapters/exr/Decoder.hpp"
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

        constexpr std::uint64_t kMaxPixels = std::uint64_t{16'384} * 16'384;
        constexpr std::uint32_t kMaxDimension = 32'768;

        // The thread count only budgets the shared presentation's row bands: the OpenEXR Core
        // decodes on the calling thread (plugins/exr/DESIGN.md, "Threading").
        core::DecoderOptions defaultOptions()
        {
            return core::DecoderOptions{std::max(1U, std::thread::hardware_concurrency()), false, kMaxPixels,
                                        kMaxDimension, true};
        }

        // Identity from the generated pvd/PluginConstants.hpp (the same values the VERSIONINFO resource
        // carries); the comments repeat the resource's text from the libraries actually linked in, and
        // the e2e version test pins the two equal.
        PluginInfo defaultInfo()
        {
            return PluginInfo{kPluginIdentity.priority, std::string{kPluginIdentity.name},
                              std::string{kPluginIdentity.version},
                              "OpenEXR decoder: " + exr::libraryVersions() + "; static build"};
        }

        // Owns the adapters, the describer, the sRGB output tables and the core plugin in dependency
        // order: `CodecPlugin` holds references to the four members declared before it, so they are
        // constructed first and destroyed last.
        class DefaultPlugin final : public IPlugin
        {
          public:
            explicit DefaultPlugin(const core::DecoderOptions &options)
                : plugin_{fileSource_, decoderFactory_, describer_, outputTables_, options, defaultInfo()}
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
            exr::DecoderFactory decoderFactory_;
            exr::Describer describer_;
            // Every EXR session presents colour (PQ codes in, sRGB out) through these tables: pure
            // math built once here, in pvdInit, so no function-local static exists anywhere in the
            // plugin (MSVC >= 14.50 would import api-ms-win-core-synch-l1-2-0.dll for its guard).
            core::colour::SrgbOutputTables outputTables_;
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
