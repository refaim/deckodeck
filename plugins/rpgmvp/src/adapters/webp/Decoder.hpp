#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include <webp/decode.h>

#include "core/IDecoder.hpp"

namespace pvdkit::rpgmvp::webp
{

    struct DecoderDestroy
    {
        void operator()(WebPIDecoder *decoder) const noexcept;
    };
    using DecoderHandle = std::unique_ptr<WebPIDecoder, DecoderDestroy>;

    namespace detail
    {
        [[nodiscard]] core::Result<void> statusResult(VP8StatusCode status, core::ErrorCode failureCode);
        [[nodiscard]] core::Result<void> prefixResult(VP8StatusCode status);
        [[nodiscard]] DecoderHandle requireDecoder(DecoderHandle decoder);
    } // namespace detail

    [[nodiscard]] std::string libraryVersions();

    class DecoderFactory;

    class Decoder final : public core::IDecoder
    {
      public:
        class Key
        {
            Key() = default;
            friend class DecoderFactory;
        };

        Decoder(Key, std::span<const std::byte> file, std::array<std::byte, 16> header, core::ImageMeta meta) noexcept;

        [[nodiscard]] const core::ImageMeta &meta() const override;
        [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t frame) const override;
        [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                     std::span<std::byte> destination,
                                                     std::uint32_t pitchBytes) override;

      private:
        std::span<const std::byte> file_;
        std::array<std::byte, 16> header_;
        core::ImageMeta meta_;
    };

    class DecoderFactory final : public core::IDecoderFactory
    {
      public:
        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;
    };

} // namespace pvdkit::rpgmvp::webp
