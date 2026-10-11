#include "core/FileSession.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "core/Transform.hpp"
#include "core/colour/Pipeline.hpp"

namespace pvdkit::core
{
    namespace
    {

        Error aborted()
        {
            return Error{ErrorCode::Aborted, "decoding was aborted by the host"};
        }

        std::uint8_t hostOrientation(const ImageMeta &meta) noexcept
        {
            if (Transform::hasTransforms(meta.transforms)) {
                return 0;
            }
            constexpr std::array<std::uint8_t, 9> exifToHost{0, 0, 2, 3, 1, 6, 7, 5, 4};
            if (meta.exifOrientation >= exifToHost.size()) {
                return 0;
            }
            return exifToHost[meta.exifOrientation];
        }

    } // namespace

    FileSession::FileSession(std::unique_ptr<IFileData> fileData, std::unique_ptr<IDecoder> decoder,
                             pvd::ImageInfo imageInfo, const DecoderOptions &options,
                             const colour::ColourTables &tables)
        : fileData_(std::move(fileData)), decoder_(std::move(decoder)), imageInfo_(std::move(imageInfo)),
          options_(options), tables_(tables)
    {
    }

    FileSession::~FileSession() = default;

    const pvd::ImageInfo &FileSession::imageInfo() const
    {
        return imageInfo_;
    }

    Result<pvd::PageInfo> FileSession::pageInfo(const std::uint32_t page) const
    {
        const auto metadata = decoder_->pageMeta(page);
        if (!metadata) {
            return std::unexpected(metadata.error());
        }
        const auto &meta = metadata->image;

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
        const auto metadata = decoder_->pageMeta(page);
        if (!metadata) {
            return std::unexpected(metadata.error());
        }
        const auto &meta = metadata->image;
        if (!progress.report(0, 3)) {
            return std::unexpected(aborted());
        }

        const bool highPrecision =
            metadata->forceHighPrecision || colour::Presentation::needed(meta.cicp, meta.chromaticities);
        const auto native = options_.nativeRgbOutput;
        const auto format = highPrecision || (options_.deepOutput && meta.depth > 8)
                                ? (native ? (meta.hasAlpha ? pvd::PixelFormat::Rgba64 : pvd::PixelFormat::Rgb48)
                                          : pvd::PixelFormat::Bgra64)
                                : (meta.hasAlpha ? (native ? pvd::PixelFormat::Rgba32 : pvd::PixelFormat::Bgra32)
                                                 : (native ? pvd::PixelFormat::Rgb24 : pvd::PixelFormat::Bgr24));
        const auto layout = pvd::pixelLayout(format);
        auto created = PixelBuffer::create(meta.width, meta.height, layout.bytesPerPixel, options_.maxPixels);
        if (!created) {
            return std::unexpected(created.error());
        }
        auto buffer = std::move(*created);

        const auto decoded = decoder_->decodeFrame(page, format, buffer.bytes(), buffer.pitchBytes());
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        const auto encoding = decoder_->decodedColour();
        if (colour::Presentation::needed(encoding.cicp, encoding.chromaticities)) {
            const colour::Presentation presentation(encoding.cicp, encoding.masteringPeakNits, encoding.chromaticities,
                                                    tables_);
            presentation.applyImage(buffer.bytes(), buffer.pitchBytes(), buffer.height(), options_.maxThreads, native,
                                    layout.bytesPerPixel / 2U);
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
        return pvd::DecodedPage{view.pixels, layout.pvdCode, view.pitchBytes, meta.hasAlpha, hostOrientation(meta)};
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

    const colour::SrgbOutputTables &FileSession::outputTables() const noexcept
    {
        return tables_.output();
    }

} // namespace pvdkit::core
