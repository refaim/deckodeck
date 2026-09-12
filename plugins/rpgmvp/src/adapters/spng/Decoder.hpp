#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <spng.h>

#include "core/IDecoder.hpp"

namespace pvdkit::rpgmvp
{

    struct ContextDestroy
    {
        void operator()(spng_ctx *context) const noexcept;
    };

    using ContextHandle = std::unique_ptr<spng_ctx, ContextDestroy>;

    [[nodiscard]] core::ErrorCode errorCodeForResult(int result) noexcept;
    [[nodiscard]] std::string libraryVersions();

    namespace detail
    {

        [[nodiscard]] core::Result<bool> chunkPresent(int result);
        [[nodiscard]] core::Result<std::pair<bool, bool>> combineChunkPresence(core::Result<bool> transparency,
                                                                               core::Result<bool> srgb);
        [[nodiscard]] core::Result<void> decodeResult(int result);

        struct ConfiguredLimits
        {
            int imageResult = 0;
            int chunkResult = 0;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::size_t chunkBytes = 0;
            std::size_t cachedChunkBytes = 0;
        };

        // Adapter test seam: observes the limits on a context created through the production path.
        [[nodiscard]] core::Result<ConfiguredLimits> configuredLimits(std::span<const std::byte> file,
                                                                      const core::DecoderOptions &options);

        class Stream
        {
          public:
            explicit Stream(std::span<const std::byte> file) noexcept;

            [[nodiscard]] bool read(std::span<std::byte> destination) noexcept;
            [[nodiscard]] std::size_t position() const noexcept;
            [[nodiscard]] std::size_t logicalSize() const noexcept;

          private:
            std::span<const std::byte> file_;
            std::size_t position_ = 0;
        };

    } // namespace detail

    [[nodiscard]] ContextHandle requireContext(ContextHandle context);
    [[nodiscard]] core::Result<void> checkDestination(const core::ImageMeta &meta, pvd::PixelFormat format,
                                                      std::size_t destinationSize, std::uint32_t pitchBytes);

    class DecoderFactory;

    class Decoder final : public core::IDecoder
    {
      public:
        class Key
        {
            Key() = default;
            friend class DecoderFactory;
        };

        Decoder(Key, std::span<const std::byte> file, core::ImageMeta meta, core::DecoderOptions options) noexcept;

        [[nodiscard]] const core::ImageMeta &meta() const override;
        [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t frame) const override;
        [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                     std::span<std::byte> destination,
                                                     std::uint32_t pitchBytes) override;

      private:
        std::span<const std::byte> file_;
        core::ImageMeta meta_;
        core::DecoderOptions options_;
    };

    class DecoderFactory final : public core::IDecoderFactory
    {
      public:
        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;
    };

} // namespace pvdkit::rpgmvp
