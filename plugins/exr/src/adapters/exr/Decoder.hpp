#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "core/IDecoder.hpp"
#include "core/PqTables.hpp"
#include "core/Windows.hpp"

namespace pvdkit::exr
{

    namespace detail
    {

        /// Confirms that `dst` holds `layout.height` rows of `pitchBytes` with room for `layout.width`
        /// BGRA64 pixels each and that the caller asked for BGRA64, the one layout this decoder
        /// produces (every EXR presents through the colour path). Violations are `Internal`.
        [[nodiscard]] core::Result<void> checkDestination(const Layout &layout, pvd::PixelFormat format,
                                                          std::size_t dstSize, std::uint32_t pitchBytes);

    } // namespace detail

    class DecoderFactory;

    /// One opened EXR: the header facts as `ImageMeta` and the picture, already decoded and encoded
    /// as PQ codes by `DecoderFactory::create` (the tone-mapping peak in the meta needs every pixel
    /// before the first page is described), composited into the display window on demand.
    class Decoder final : public core::IDecoder
    {
      public:
        /// Passkey: only `DecoderFactory` constructs a `Decoder`, from pixels it has already read.
        class Key
        {
            Key() = default;
            friend class DecoderFactory;
        };

        Decoder(Key, core::ImageMeta meta, const Layout &layout, std::unique_ptr<std::uint16_t[]> overlap,
                std::size_t overlapCodes) noexcept;

        [[nodiscard]] const core::ImageMeta &meta() const override;
        [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t frame) const override;
        [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                     std::span<std::byte> dst, std::uint32_t pitchBytes) override;

      private:
        core::ImageMeta meta_;
        Layout layout_;
        std::unique_ptr<std::uint16_t[]> overlap_;
        std::size_t overlapCodes_;
    };

    class DecoderFactory final : public core::IDecoderFactory
    {
      public:
        /// The OpenEXR magic number `76 2f 31 01` followed by format version 2, in a head of at
        /// least 16 bytes.
        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        /// Parses the header, picks the part and channels, reads and encodes every pixel of the
        /// display window and measures the tone-mapping peak. Nothing of the library outlives this
        /// call: the returned decoder holds only the encoded picture, so `file` may be released
        /// afterwards (FileSession keeps it anyway).
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;

      private:
        /// The exact PQ encoder every decode borrows: built once with the factory, i.e. in pvdInit.
        PqCodeTables tables_;
    };

} // namespace pvdkit::exr
