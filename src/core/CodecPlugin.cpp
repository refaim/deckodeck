#include "core/CodecPlugin.hpp"

#include <cstddef>
#include <span>
#include <utility>

#include "core/FileSession.hpp"

namespace pvdkit::core
{

    CodecPlugin::CodecPlugin(IFileSource &fileSource, IDecoderFactory &decoderFactory, const IImageDescriber &describer,
                             const colour::SrgbOutputTables &outputTables, const DecoderOptions &options,
                             pvd::PluginInfo pluginInfo)
        : fileSource_(fileSource), decoderFactory_(decoderFactory), describer_(describer), outputTables_(outputTables),
          options_(options), pluginInfo_(std::move(pluginInfo))
    {
    }

    const pvd::PluginInfo &CodecPlugin::info() const
    {
        return pluginInfo_;
    }

    Result<std::unique_ptr<pvd::IFileSession>> CodecPlugin::open(const pvd::OpenRequest &request)
    {
        if (!decoderFactory_.recognises(request.head)) {
            return std::unexpected(Error{ErrorCode::NotRecognised, "input does not carry a recognised signature"});
        }

        std::unique_ptr<IFileData> fileData;
        std::span<const std::byte> bytes = request.head;
        if (request.fileSize != 0) {
            auto opened = fileSource_.open(request.utf8FileName);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            fileData = std::move(*opened);
            bytes = fileData->bytes();
        }

        auto decoder = decoderFactory_.create(bytes, options_);
        if (!decoder) {
            return std::unexpected(decoder.error());
        }

        const auto &meta = (*decoder)->meta();
        auto description = describer_.describe(meta);
        pvd::ImageInfo imageInfo{meta.frameCount, meta.animated, std::move(description.formatName),
                                 std::move(description.compression), std::move(description.comments)};
        std::unique_ptr<pvd::IFileSession> session = std::make_unique<FileSession>(
            std::move(fileData), std::move(*decoder), std::move(imageInfo), options_, outputTables_);
        return session;
    }

} // namespace pvdkit::core
