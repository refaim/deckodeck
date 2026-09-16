#include "adapters/spng/Decoder.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <zlib.h>

#include "core/Format.hpp"

namespace pvdkit::rpgmvp::detail
{

    Stream::Stream(const std::span<const std::byte> file) noexcept : file_(file)
    {
    }

    bool Stream::read(const std::span<std::byte> destination) noexcept
    {
        if (destination.size() > logicalSize() - position_) {
            return false;
        }

        auto remaining = destination;
        if (position_ < kPngHeader.size()) {
            const auto headerBytes = std::min(remaining.size(), kPngHeader.size() - position_);
            std::ranges::copy(std::span{kPngHeader}.subspan(position_, headerBytes), remaining.begin());
            position_ += headerBytes;
            remaining = remaining.subspan(headerBytes);
        }
        if (!remaining.empty()) {
            const auto sourceOffset = 32 + position_ - kPngHeader.size();
            std::ranges::copy(file_.subspan(sourceOffset, remaining.size()), remaining.begin());
            position_ += remaining.size();
        }
        return true;
    }

    std::size_t Stream::position() const noexcept
    {
        return position_;
    }

    std::size_t Stream::logicalSize() const noexcept
    {
        return file_.size() - kPngHeader.size();
    }

} // namespace pvdkit::rpgmvp::detail

namespace pvdkit::rpgmvp
{

    namespace
    {

        constexpr std::size_t kMaximumChunkBytes = std::size_t{16} * 1024U * 1024U;
        constexpr std::size_t kMaximumCachedChunkBytes = std::size_t{64} * 1024U * 1024U;
        static_assert(kMaximumChunkBytes <= kMaximumCachedChunkBytes);
        static_assert(kMaximumChunkBytes <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()));

        core::Error resultError(const int result, const core::ErrorCode code)
        {
            return {code, spng_strerror(result)};
        }

        int readCallback(spng_ctx *, void *user, void *destination, const std::size_t length) noexcept
        {
            auto &stream = *static_cast<detail::Stream *>(user);
            const auto bytes = std::span{static_cast<std::byte *>(destination), length};
            static_assert(noexcept(stream.read(bytes)));
            return stream.read(bytes) ? SPNG_OK : SPNG_IO_EOF;
        }

        struct ContextBundle
        {
            explicit ContextBundle(const std::span<const std::byte> file)
                : stream{file}, context{requireContext(ContextHandle{spng_ctx_new(0)})}
            {
            }

            ContextBundle(const ContextBundle &) = delete;
            ContextBundle &operator=(const ContextBundle &) = delete;
            ContextBundle(ContextBundle &&) = delete;
            ContextBundle &operator=(ContextBundle &&) = delete;

            detail::Stream stream;
            ContextHandle context;
        };

        core::Result<void> settingResult(const int result, const std::string_view setting)
        {
            if (result == SPNG_OK) {
                return {};
            }
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, std::string{setting} + ": " + spng_strerror(result)});
        }

        core::Result<std::unique_ptr<ContextBundle>> makeContext(const std::span<const std::byte> file,
                                                                 const core::DecoderOptions &options)
        {
            auto bundle = std::make_unique<ContextBundle>(file);
            return settingResult(
                       spng_set_image_limits(bundle->context.get(), options.maxDimension, options.maxDimension),
                       "spng_set_image_limits")
                .and_then([&] {
                    return settingResult(
                        spng_set_chunk_limits(bundle->context.get(), kMaximumChunkBytes, kMaximumCachedChunkBytes),
                        "spng_set_chunk_limits");
                })
                .and_then([&] {
                    return settingResult(spng_set_crc_action(bundle->context.get(), SPNG_CRC_ERROR, SPNG_CRC_ERROR),
                                         "spng_set_crc_action");
                })
                .and_then([&] {
                    return settingResult(spng_set_png_stream(bundle->context.get(), readCallback, &bundle->stream),
                                         "spng_set_png_stream");
                })
                .transform([&] { return std::move(bundle); });
        }

        core::ImageMeta imageMeta(const spng_ihdr &ihdr, const bool transparency, const bool srgb)
        {
            const bool greyscale =
                ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE || ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA;
            const bool directAlpha = ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA ||
                                     ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;

            core::ImageMeta meta{};
            meta.width = ihdr.width;
            meta.height = ihdr.height;
            meta.depth = ihdr.bit_depth;
            meta.chroma = greyscale ? core::ChromaFormat::Yuv400 : core::ChromaFormat::Yuv444;
            meta.hasAlpha = directAlpha || transparency;
            meta.alphaPremultiplied = false;
            meta.cicp = srgb ? core::Cicp{1, 13, 0, true} : core::Cicp{2, 2, 0, true};
            meta.frameCount = 1;
            meta.animated = false;
            meta.indexed = ihdr.color_type == SPNG_COLOR_TYPE_INDEXED;
            meta.interlaced = ihdr.interlace_method == SPNG_INTERLACE_ADAM7;
            return meta;
        }

        core::Result<bool> hasIccProfile(spng_ctx &context)
        {
            spng_iccp profile{};
            return detail::chunkPresent(spng_get_iccp(&context, &profile));
        }

    } // namespace

    void ContextDestroy::operator()(spng_ctx *context) const noexcept
    {
        spng_ctx_free(context);
    }

    core::ErrorCode errorCodeForResult(const int result) noexcept
    {
        if (result == SPNG_EUSER_WIDTH || result == SPNG_EUSER_HEIGHT || result == SPNG_EOVERFLOW ||
            result == SPNG_ECHUNK_LIMITS) {
            return core::ErrorCode::TooLarge;
        }
        return core::ErrorCode::ParseFailed;
    }

    core::Result<bool> detail::chunkPresent(const int result)
    {
        if (result == SPNG_OK) {
            return true;
        }
        if (result == SPNG_ECHUNKAVAIL) {
            return false;
        }
        return std::unexpected(resultError(result, errorCodeForResult(result)));
    }

    core::Result<std::pair<bool, bool>> detail::combineChunkPresence(core::Result<bool> transparency,
                                                                     core::Result<bool> srgb)
    {
        if (!transparency) {
            return std::unexpected(transparency.error());
        }
        if (!srgb) {
            return std::unexpected(srgb.error());
        }
        return std::pair{*transparency, *srgb};
    }

    core::Result<void> detail::decodeResult(const int result)
    {
        if (result == SPNG_OK) {
            return {};
        }
        const core::ErrorCode mapped = errorCodeForResult(result);
        const core::ErrorCode code = mapped == core::ErrorCode::TooLarge ? mapped : core::ErrorCode::DecodeFailed;
        return std::unexpected(resultError(result, code));
    }

    core::Result<detail::ConfiguredLimits> detail::configuredLimits(const std::span<const std::byte> file,
                                                                    const core::DecoderOptions &options)
    {
        return makeContext(file, options).transform([](const auto &bundle) {
            ConfiguredLimits limits{};
            limits.imageResult = spng_get_image_limits(bundle->context.get(), &limits.width, &limits.height);
            limits.chunkResult =
                spng_get_chunk_limits(bundle->context.get(), &limits.chunkBytes, &limits.cachedChunkBytes);
            return limits;
        });
    }

    std::string libraryVersions()
    {
        return std::string{"libspng "} + spng_version_string() + ", zlib " + zlibVersion();
    }

    ContextHandle requireContext(ContextHandle context)
    {
        if (!context) {
            throw std::bad_alloc{};
        }
        return context;
    }

    core::Result<void> checkDestination(const core::ImageMeta &meta, const pvd::PixelFormat format,
                                        const std::size_t destinationSize, const std::uint32_t pitchBytes)
    {
        if (format == pvd::PixelFormat::Bgra64) {
            if (meta.depth <= 8) {
                return std::unexpected(core::Error{core::ErrorCode::UnsupportedFeature,
                                                   "BGRA64 output requires a source deeper than 8 bits per sample"});
            }
        } else {
            const pvd::PixelFormat expected = meta.hasAlpha ? pvd::PixelFormat::Bgra32 : pvd::PixelFormat::Bgr24;
            if (format != expected) {
                return std::unexpected(
                    core::Error{core::ErrorCode::Internal, "caller pixel format contradicts metadata"});
            }
        }
        const std::uint32_t bytesPerPixel =
            format == pvd::PixelFormat::Bgra64 ? 8U : (format == pvd::PixelFormat::Bgra32 ? 4U : 3U);
        const std::uint64_t expectedPitch = static_cast<std::uint64_t>(meta.width) * bytesPerPixel;
        if (pitchBytes != expectedPitch) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller pitch is not one tightly packed row"});
        }
        const std::uint64_t expectedSize = expectedPitch * meta.height;
        if (destinationSize < expectedSize) {
            return std::unexpected(core::Error{core::ErrorCode::Internal, "caller buffer is smaller than the image"});
        }
        return {};
    }

    Decoder::Decoder(Key, const std::span<const std::byte> file, core::ImageMeta meta,
                     const core::DecoderOptions options) noexcept
        : file_(file), meta_(std::move(meta)), options_(options)
    {
    }

    const core::ImageMeta &Decoder::meta() const
    {
        return meta_;
    }

    core::Result<core::FrameTiming> Decoder::frameTiming(const std::uint32_t frame) const
    {
        if (frame != 0) {
            return std::unexpected(core::Error{core::ErrorCode::PageOutOfRange, "RPGMVP contains one still image"});
        }
        return core::FrameTiming{0};
    }

    core::Result<void> Decoder::decodeFrame(const std::uint32_t frame, const pvd::PixelFormat format,
                                            const std::span<std::byte> destination, const std::uint32_t pitchBytes)
    {
        if (frame != 0) {
            return std::unexpected(core::Error{core::ErrorCode::PageOutOfRange, "RPGMVP contains one still image"});
        }
        return checkDestination(meta_, format, destination.size(), pitchBytes).and_then([&] {
            return makeContext(file_, options_).and_then([&](const auto &bundle) {
                const int spngFormat = format == pvd::PixelFormat::Bgra64
                                           ? SPNG_FMT_RGBA16
                                           : (meta_.hasAlpha ? SPNG_FMT_RGBA8 : SPNG_FMT_RGB8);
                std::size_t decodedSize = 0;
                const int sizeResult = spng_decoded_image_size(bundle->context.get(), spngFormat, &decodedSize);
                return detail::decodeResult(sizeResult).and_then([&] {
                    const int result = spng_decode_image(bundle->context.get(), destination.data(), decodedSize,
                                                         spngFormat, SPNG_DECODE_TRNS);
                    return detail::decodeResult(result).transform([&] {
                        // SPNG_FMT_RGBA16 returns host-endian samples (spng.h explicitly reserves
                        // big-endian for SPNG_FMT_RAW). Windows hosts are little-endian, so swapping
                        // the two-byte R and B units preserves each sample's byte order.
                        const std::size_t channelBytes = format == pvd::PixelFormat::Bgra64 ? 2U : 1U;
                        const std::size_t bytesPerPixel =
                            format == pvd::PixelFormat::Bgra64 ? 8U : (meta_.hasAlpha ? 4U : 3U);
                        for (std::size_t offset = 0; offset < decodedSize; offset += bytesPerPixel) {
                            for (std::size_t byte = 0; byte < channelBytes; ++byte) {
                                std::swap(destination[offset + byte], destination[offset + 2U * channelBytes + byte]);
                            }
                        }
                    });
                });
            });
        });
    }

    bool DecoderFactory::recognises(const std::span<const std::byte> head) const
    {
        return rpgmvp::recognises(head);
    }

    core::Result<std::unique_ptr<core::IDecoder>> DecoderFactory::create(const std::span<const std::byte> file,
                                                                         const core::DecoderOptions &options)
    {
        if (!recognises(file)) {
            return std::unexpected(core::Error{core::ErrorCode::NotRecognised, "not an RPG Maker encrypted PNG"});
        }

        return makeContext(file, options)
            .and_then([&](const auto &bundle) -> core::Result<std::unique_ptr<core::IDecoder>> {
                spng_ihdr ihdr{};
                const int headerResult = spng_get_ihdr(bundle->context.get(), &ihdr);
                if (headerResult != SPNG_OK) {
                    return std::unexpected(resultError(headerResult, errorCodeForResult(headerResult)));
                }

                spng_trns transparency{};
                const auto hasTransparency = detail::chunkPresent(spng_get_trns(bundle->context.get(), &transparency));
                std::uint8_t renderingIntent = 0;
                const auto hasSrgb = detail::chunkPresent(spng_get_srgb(bundle->context.get(), &renderingIntent));
                return detail::combineChunkPresence(hasTransparency, hasSrgb).and_then([&](const auto presence) {
                    return hasIccProfile(*bundle->context).transform([&](const bool icc) {
                        core::ImageMeta meta = imageMeta(ihdr, presence.first, presence.second);
                        meta.hasIcc = icc;
                        return std::unique_ptr<core::IDecoder>{
                            std::make_unique<Decoder>(Decoder::Key{}, file, meta, options)};
                    });
                });
            });
    }

} // namespace pvdkit::rpgmvp
