#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "core/IDecoder.hpp"

namespace pvdkit::rpgmvp::webp
{

    enum class WebpKind : std::uint8_t
    {
        Lossless,
        Lossy,
        Extended
    };

    struct WebpImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool hasAlpha = false;
        bool animated = false;
        std::uint8_t coding = 0;
        std::uint8_t flags = 0;
    };

    // Only payload signatures are inspected; the codec validates the reconstructed container.
    [[nodiscard]] std::optional<WebpKind> detectWebp(std::span<const std::byte> head) noexcept;
    [[nodiscard]] core::Result<std::array<std::byte, 16>> reconstructedWebpHeader(WebpKind kind,
                                                                                  std::uint64_t encryptedSize);
    [[nodiscard]] core::Result<core::ImageMeta> normaliseWebp(const WebpImage &image, WebpKind kind,
                                                              const core::DecoderOptions &options);
    [[nodiscard]] core::Result<void> checkWebpDestination(const core::ImageMeta &meta, pvd::PixelFormat format,
                                                          std::size_t destinationSize, std::uint32_t pitchBytes);

} // namespace pvdkit::rpgmvp::webp
