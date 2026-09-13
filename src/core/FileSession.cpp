#include "core/FileSession.hpp"

#include <algorithm>
#include <utility>

#include "core/Transform.hpp"
#include "core/colour/Pipeline.hpp"

namespace pvdkit::core
{
    namespace
    {

        Error outOfRange()
        {
            return Error{ErrorCode::PageOutOfRange, "page index is out of range"};
        }

        Error aborted()
        {
            return Error{ErrorCode::Aborted, "decoding was aborted by the host"};
        }

    } // namespace

    FileSession::FileSession(std::unique_ptr<IFileData> fileData, std::unique_ptr<IDecoder> decoder,
                             pvd::ImageInfo imageInfo, const DecoderOptions &options)
        : fileData_(std::move(fileData)), decoder_(std::move(decoder)), imageInfo_(std::move(imageInfo)),
          options_(options)
    {
    }

    FileSession::~FileSession() = default;

    const pvd::ImageInfo &FileSession::imageInfo() const
    {
        return imageInfo_;
    }

    Result<pvd::PageInfo> FileSession::pageInfo(const std::uint32_t page) const
    {
        const auto &meta = decoder_->meta();
        if (page >= meta.frameCount) {
            return std::unexpected(outOfRange());
        }

        std::uint32_t frameTimeMs = 0;
        if (meta.animated) {
            const auto timing = decoder_->frameTiming(page);
            if (!timing) {
                return std::unexpected(timing.error());
            }
            frameTimeMs = timing->durationMs;
        }

        const auto [width, height] = Transform::displaySize(meta);
        const auto channels = meta.hasAlpha ? 4U : 3U;
        const auto bitsPerPixel =
            meta.indexed ? static_cast<std::uint32_t>(meta.depth) : static_cast<std::uint32_t>(meta.depth) * channels;
        return pvd::PageInfo{width, height, bitsPerPixel, frameTimeMs};
    }

    Result<pvd::DecodedPage> FileSession::decodePage(const std::uint32_t page, const pvd::Progress &progress)
    {
        const auto &meta = decoder_->meta();
        if (page >= meta.frameCount) {
            return std::unexpected(outOfRange());
        }
        if (!progress.report(0, 3)) {
            return std::unexpected(aborted());
        }

        const bool presentationNeeded = colour::Presentation::needed(meta.cicp);
        const auto format = presentationNeeded || (options_.deepOutput && meta.depth > 8)
                                ? pvd::PixelFormat::Bgra64
                                : (meta.hasAlpha ? pvd::PixelFormat::Bgra32 : pvd::PixelFormat::Bgr24);
        const auto bytesPerPixel =
            format == pvd::PixelFormat::Bgra64 ? 8U : (format == pvd::PixelFormat::Bgra32 ? 4U : 3U);
        auto created = PixelBuffer::create(meta.width, meta.height, bytesPerPixel, options_.maxPixels);
        if (!created) {
            return std::unexpected(created.error());
        }
        auto buffer = std::move(*created);

        const auto decoded = decoder_->decodeFrame(page, format, buffer.bytes(), buffer.pitchBytes());
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (presentationNeeded) {
            const colour::Presentation presentation{meta.cicp, meta.masteringPeakNits};
            const auto bytesPerRow = static_cast<std::size_t>(buffer.pitchBytes());
            auto bytes = buffer.bytes();
            for (std::uint32_t row = 0; row < buffer.height(); ++row) {
                presentation.apply(bytes.subspan(static_cast<std::size_t>(row) * bytesPerRow, bytesPerRow));
            }
        }
        if (!progress.report(1, 3)) {
            return std::unexpected(aborted());
        }

        if (Transform::hasTransforms(meta.transforms)) {
            auto transformed = Transform::apply(meta.transforms, buffer.view(), options_.maxPixels);
            if (!transformed) {
                return std::unexpected(transformed.error());
            }
            buffer = std::move(*transformed);
        }

        if (!progress.report(2, 3)) {
            return std::unexpected(aborted());
        }

        auto retained = std::make_unique<PixelBuffer>(std::move(buffer));
        const auto view = retained->view();
        outstanding_.push_back(std::move(retained));
        return pvd::DecodedPage{view.pixels, view.bytesPerPixel * 8, view.pitchBytes, meta.hasAlpha,
                                decoder_->iccProfile()};
    }

    bool FileSession::freePage(const std::span<const std::byte> pixels)
    {
        const auto found = std::find_if(outstanding_.begin(), outstanding_.end(),
                                        [&](const auto &page) { return page->bytes().data() == pixels.data(); });
        if (found == outstanding_.end()) {
            return false;
        }
        outstanding_.erase(found);
        return true;
    }

} // namespace pvdkit::core
