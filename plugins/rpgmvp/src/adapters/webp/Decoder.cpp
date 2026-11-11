#include "adapters/webp/Decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "core/Webp.hpp"

namespace pvdkit::rpgmvp::webp
{
    namespace
    {
        core::Result<WebpImage> readFeatures(const std::span<const std::byte> file,
                                             const std::array<std::byte, 16> &header)
        {
            std::array<std::byte, 64> prefix{};
            std::ranges::copy(header, prefix.begin());
            const auto tail = file.subspan(32).first(std::min(file.size() - 32U, prefix.size() - header.size()));
            std::ranges::copy(tail, prefix.begin() + 16);
            WebPBitstreamFeatures features{};
            const auto status = WebPGetFeatures(reinterpret_cast<const std::uint8_t *>(prefix.data()),
                                                header.size() + tail.size(), &features);
            return detail::statusResult(status, core::ErrorCode::ParseFailed).transform([&] {
                return WebpImage{static_cast<std::uint32_t>(features.width),
                                 static_cast<std::uint32_t>(features.height),
                                 features.has_alpha != 0,
                                 features.has_animation != 0,
                                 static_cast<std::uint8_t>(features.format),
                                 std::to_integer<std::uint8_t>(file[36])};
            });
        }
    } // namespace

    void DecoderDestroy::operator()(WebPIDecoder *decoder) const noexcept
    {
        WebPIDelete(decoder);
    }

    core::Result<void> detail::statusResult(const VP8StatusCode status, const core::ErrorCode failureCode)
    {
        if (status == VP8_STATUS_OK) {
            return {};
        }
        if (status == VP8_STATUS_OUT_OF_MEMORY) {
            throw std::bad_alloc{};
        }
        const auto code = status == VP8_STATUS_INVALID_PARAM         ? core::ErrorCode::Internal
                          : status == VP8_STATUS_UNSUPPORTED_FEATURE ? core::ErrorCode::UnsupportedFeature
                                                                     : failureCode;
        return std::unexpected(core::Error{code, "libwebp status " + std::to_string(static_cast<int>(status))});
    }

    core::Result<void> detail::prefixResult(const VP8StatusCode status)
    {
        if (status == VP8_STATUS_SUSPENDED) {
            return {};
        }
        return statusResult(status, core::ErrorCode::DecodeFailed);
    }

    DecoderHandle detail::requireDecoder(DecoderHandle decoder)
    {
        if (!decoder) {
            throw std::bad_alloc{};
        }
        return decoder;
    }

    std::string libraryVersions()
    {
        const auto version = static_cast<unsigned>(WebPGetDecoderVersion());
        return "libwebp " + std::to_string(version >> 16U) + "." + std::to_string((version >> 8U) & 0xffU) + "." +
               std::to_string(version & 0xffU);
    }

    Decoder::Decoder(Key, const std::span<const std::byte> file, const std::array<std::byte, 16> header,
                     core::ImageMeta meta) noexcept
        : file_{file}, header_{header}, meta_{std::move(meta)}
    {
    }

    const core::ImageMeta &Decoder::meta() const
    {
        return meta_;
    }

    core::Result<core::FrameTiming> Decoder::frameTiming(const std::uint32_t frame) const
    {
        if (frame != 0) {
            return std::unexpected(
                core::Error{core::ErrorCode::PageOutOfRange, "encrypted WebP contains one still image"});
        }
        return core::FrameTiming{0};
    }

    core::Result<void> Decoder::decodeFrame(const std::uint32_t frame, const pvd::PixelFormat format,
                                            const std::span<std::byte> destination, const std::uint32_t pitchBytes)
    {
        if (frame != 0) {
            return std::unexpected(
                core::Error{core::ErrorCode::PageOutOfRange, "encrypted WebP contains one still image"});
        }
        return checkWebpDestination(meta_, format, destination.size(), pitchBytes).and_then([&] {
            constexpr std::array modes{MODE_BGR, MODE_BGRA, MODE_LAST, MODE_RGB, MODE_RGBA, MODE_LAST, MODE_LAST};
            auto decoder = detail::requireDecoder(DecoderHandle{WebPINewRGB(
                modes.at(static_cast<std::size_t>(format)), reinterpret_cast<std::uint8_t *>(destination.data()),
                destination.size(), static_cast<int>(pitchBytes))});
            return detail::prefixResult(WebPIAppend(decoder.get(),
                                                    reinterpret_cast<const std::uint8_t *>(header_.data()),
                                                    header_.size()))
                .and_then([&] {
                    // Incremental libwebp owns its compressed-input scratch buffer; the mapped
                    // source and the caller's straight RGB/RGBA output keep independent owners.
                    return detail::statusResult(
                        WebPIAppend(decoder.get(), reinterpret_cast<const std::uint8_t *>(file_.subspan(32).data()),
                                    file_.size() - 32U),
                        core::ErrorCode::DecodeFailed);
                });
        });
    }

    bool DecoderFactory::recognises(const std::span<const std::byte> head) const
    {
        return detectWebp(head).has_value();
    }

    core::Result<std::unique_ptr<core::IDecoder>> DecoderFactory::create(const std::span<const std::byte> file,
                                                                         const core::DecoderOptions &options)
    {
        const auto kind = detectWebp(file);
        if (!kind) {
            return std::unexpected(core::Error{core::ErrorCode::NotRecognised, "not an RPG Maker encrypted WebP"});
        }
        return reconstructedWebpHeader(*kind, file.size()).and_then([&](const auto &header) {
            return readFeatures(file, header).and_then([&](const auto &features) {
                return normaliseWebp(features, *kind, options).transform([&](core::ImageMeta meta) {
                    return std::unique_ptr<core::IDecoder>{
                        std::make_unique<Decoder>(Decoder::Key{}, file, header, std::move(meta))};
                });
            });
        });
    }

} // namespace pvdkit::rpgmvp::webp
