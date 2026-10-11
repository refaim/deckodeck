#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "adapters/exr/Context.hpp"
#include "adapters/exr/Pixels.hpp"
#include "core/Colour.hpp"
#include "core/IDecoder.hpp"
#include "core/Pages.hpp"
#include "core/PqTables.hpp"
#include "core/Windows.hpp"

namespace pvdkit::exr
{
    namespace detail
    {
        [[nodiscard]] core::Result<void> checkDestination(const Layout &layout, pvd::PixelFormat format,
                                                          std::size_t dstSize, std::uint32_t pitchBytes,
                                                          pvd::PixelFormat selectedFormat = pvd::PixelFormat::Bgra64);
    }

    /// Header-only catalogue entry; pixels are kept only for the current page.
    struct Page
    {
        PageChoice choice;
        PartHeader header;
        ColourSignal colour;
        Layout layout;
        core::ImageMeta meta;
        pvd::PixelFormat format = pvd::PixelFormat::Bgra64;
    };

    class DecoderFactory;

    class Decoder final : public core::IDecoder
    {
      public:
        class Key
        {
            Key() = default;
            friend class DecoderFactory;
        };

        Decoder(Key, std::vector<Page> pages, std::span<const std::byte> file, const core::DecoderOptions &options,
                const PqCodeTables &tables, CachedPixels pixels) noexcept;
        Decoder(const Decoder &) = delete;
        Decoder &operator=(const Decoder &) = delete;
        Decoder(Decoder &&) = delete;
        Decoder &operator=(Decoder &&) = delete;
        [[nodiscard]] const core::ImageMeta &meta() const override;
        [[nodiscard]] core::Result<core::PageMeta> pageMeta(std::uint32_t page) const override;
        [[nodiscard]] core::ColourEncoding decodedColour() const override;
        [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t frame) const override;
        [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                     std::span<std::byte> dst, std::uint32_t pitchBytes) override;

      private:
        [[nodiscard]] core::Result<void> loadPage(std::uint32_t page);
        std::vector<Page> pages_;
        std::span<const std::byte> file_;
        core::DecoderOptions options_;
        const PqCodeTables &tables_;
        std::unique_ptr<std::uint16_t[]> overlap_;
        std::size_t overlapCodes_;
        std::optional<std::uint32_t> current_ = 0;
        core::ColourEncoding decodedColour_;
    };

    class DecoderFactory final : public core::IDecoderFactory
    {
      public:
        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        /// Page zero is decoded eagerly; file bytes and this factory's tables must outlive Decoder.
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;

      private:
        PqCodeTables tables_;
    };
} // namespace pvdkit::exr
