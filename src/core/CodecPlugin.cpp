#include "core/CodecPlugin.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

#include "core/FileSession.hpp"

namespace pvdkit::core
{

    CodecPlugin::CodecPlugin(IFileSource &fileSource, IDecoderFactory &decoderFactory, const IImageDescriber &describer,
                             const colour::ColourTables &tables, const DecoderOptions &options,
                             pvd::PluginInfo pluginInfo)
        : fileSource_(fileSource), decoderFactory_(decoderFactory), describer_(describer), tables_(tables),
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
            auto opened =
                request.wideFileName ? fileSource_.open(*request.wideFileName) : fileSource_.open(request.utf8FileName);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            fileData = std::move(*opened);
            bytes = fileData->bytes();
        }

        auto sessionOptions = options_;
        sessionOptions.nativeRgbOutput = options_.nativeRgbOutput && request.nativeRgb;
        sessionOptions.sourcePath = std::nullopt;
        if (fileData) {
            sessionOptions.sourcePath = fileData->sourcePath();
        }
        auto decoder = decoderFactory_.create(bytes, sessionOptions);
        if (!decoder) {
            return std::unexpected(decoder.error());
        }

        const auto &meta = (*decoder)->meta();
        auto description = describer_.describe(meta);
        pvd::ImageInfo imageInfo{meta.frameCount, meta.animated, std::move(description.formatName),
                                 std::move(description.compression), std::move(description.comments)};
        std::unique_ptr<pvd::IFileSession> session = std::make_unique<FileSession>(
            std::move(fileData), std::move(*decoder), std::move(imageInfo), sessionOptions, tables_);
        return session;
    }

} // namespace pvdkit::core
