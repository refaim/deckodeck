#include "core/Webp.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace pvdkit::rpgmvp::webp
{
    namespace
    {
        constexpr std::array kRpgSignature{std::byte{'R'}, std::byte{'P'}, std::byte{'G'}, std::byte{'M'},
                                           std::byte{'V'}, std::byte{0},   std::byte{0},   std::byte{0}};
        constexpr std::array kVp8Signature{std::byte{0x9d}, std::byte{0x01}, std::byte{0x2a}};
        constexpr std::array kExtendedLength{std::byte{10}, std::byte{0}, std::byte{0}, std::byte{0}};
        constexpr std::array<std::byte, 3> kReserved{};
    } // namespace

    std::optional<WebpKind> detectWebp(const std::span<const std::byte> head) noexcept
    {
        if (head.size() < 41 || !std::ranges::equal(head.first(8), kRpgSignature)) {
            return std::nullopt;
        }
        if (head[36] == std::byte{0x2f}) {
            return WebpKind::Lossless;
        }
        if (head.size() < 46) {
            return std::nullopt;
        }
        if (std::ranges::equal(head.subspan<39, 3>(), kVp8Signature)) {
            return WebpKind::Lossy;
        }
        if (std::ranges::equal(head.subspan<32, 4>(), kExtendedLength) &&
            (std::to_integer<unsigned>(head[36]) & 0xc1U) == 0 &&
            std::ranges::equal(head.subspan<37, 3>(), kReserved)) {
            return WebpKind::Extended;
        }
        return std::nullopt;
    }

    core::Result<std::array<std::byte, 16>> reconstructedWebpHeader(const WebpKind kind,
                                                                    const std::uint64_t encryptedSize)
    {
        if (encryptedSize < 41) {
            return std::unexpected(core::Error{core::ErrorCode::NotRecognised, "encrypted WebP header is incomplete"});
        }
        const auto riffSize = encryptedSize - 24U;
        if (riffSize > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(
                core::Error{core::ErrorCode::TooLarge, "encrypted WebP exceeds the RIFF size field"});
        }
        constexpr std::array suffixes{std::byte{'L'}, std::byte{' '}, std::byte{'X'}};
        std::array result{std::byte{'R'}, std::byte{'I'}, std::byte{'F'}, std::byte{'F'},
                          std::byte{0},   std::byte{0},   std::byte{0},   std::byte{0},
                          std::byte{'W'}, std::byte{'E'}, std::byte{'B'}, std::byte{'P'},
                          std::byte{'V'}, std::byte{'P'}, std::byte{'8'}, suffixes.at(static_cast<std::size_t>(kind))};
        for (unsigned index = 0; index < 4; ++index) {
            result[4U + index] = std::byte{static_cast<unsigned char>((riffSize >> (index * 8U)) & 0xffU)};
        }
        return result;
    }

    core::Result<core::ImageMeta> normaliseWebp(const WebpImage &image, const WebpKind kind,
                                                const core::DecoderOptions &options)
    {
        if (image.animated) {
            return std::unexpected(
                core::Error{core::ErrorCode::UnsupportedFeature, "animated encrypted WebP is unsupported"});
        }
        if (options.maxDimension != 0 && std::max(image.width, image.height) > options.maxDimension) {
            return std::unexpected(
                core::Error{core::ErrorCode::TooLarge, "encrypted WebP exceeds the dimension limit"});
        }
        if (options.maxPixels != 0 && static_cast<std::uint64_t>(image.width) * image.height > options.maxPixels) {
            return std::unexpected(core::Error{core::ErrorCode::TooLarge, "encrypted WebP exceeds the pixel limit"});
        }
        core::ImageMeta meta{};
        meta.width = image.width;
        meta.height = image.height;
        meta.depth = 8;
        meta.hasAlpha = image.hasAlpha;
        meta.cicp = {1, 13, 0, true};
        meta.frameCount = 1;
        // VP8X features can precede a metadata chunk larger than the inspected prefix;
        // libwebp reports coding 0 until it has seen the actual VP8/VP8L chunk.
        constexpr std::array<std::string_view, 3> names{"WebP", "WebP lossy", "WebP lossless"};
        meta.compression = names.at(image.coding);
        const auto flags = kind == WebpKind::Extended ? image.flags : 0;
        meta.hasIcc = (flags & 0x20) != 0;
        meta.hasExif = (flags & 0x08) != 0;
        meta.hasXmp = (flags & 0x04) != 0;
        return meta;
    }

    core::Result<void> checkWebpDestination(const core::ImageMeta &meta, const pvd::PixelFormat format,
                                            const std::size_t destinationSize, const std::uint32_t pitchBytes)
    {
        const auto legacy = meta.hasAlpha ? pvd::PixelFormat::Bgra32 : pvd::PixelFormat::Bgr24;
        const auto native = meta.hasAlpha ? pvd::PixelFormat::Rgba32 : pvd::PixelFormat::Rgb24;
        if (format != legacy && format != native) {
            return std::unexpected(core::Error{core::ErrorCode::Internal, "caller pixel format contradicts metadata"});
        }
        const auto expectedPitch = static_cast<std::uint64_t>(meta.width) * pvd::pixelLayout(format).bytesPerPixel;
        if (pitchBytes != expectedPitch) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller pitch is not one tightly packed row"});
        }
        if (destinationSize < expectedPitch * meta.height) {
            return std::unexpected(core::Error{core::ErrorCode::Internal, "caller buffer is smaller than the image"});
        }
        return {};
    }

} // namespace pvdkit::rpgmvp::webp
