#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include <avif/avif.h>

#include "core/IDecoder.hpp"

namespace pvdkit::avif
{

    struct DecoderDestroy
    {
        void operator()(avifDecoder *decoder) const noexcept;
    };

    using DecoderHandle = std::unique_ptr<avifDecoder, DecoderDestroy>;

    [[nodiscard]] core::ErrorCode errorCodeForResult(avifResult result) noexcept;
    [[nodiscard]] std::string libraryVersions();

    // Internal helpers exposed for unit tests; not part of the adapter contract.
    namespace detail
    {

        [[nodiscard]] core::Result<core::ChromaFormat> chromaFormat(avifPixelFormat format);
        [[nodiscard]] core::Result<core::Transforms> transforms(const avifImage &image, avifDiagnostics &diagnostics);
        [[nodiscard]] core::Result<void> checkedResult(avifResult result, core::ErrorCode code,
                                                       const avifDiagnostics &diagnostics);
        [[nodiscard]] std::uint32_t durationMilliseconds(double durationSeconds);
        [[nodiscard]] DecoderHandle requireDecoder(DecoderHandle decoder);
        /// Confirms that `dstSize` bytes with `pitchBytes` per row hold `meta.height` rows of
        /// `meta.width` pixels in `format`; the size arithmetic is 64-bit. Violations are `Internal`.
        [[nodiscard]] core::Result<void> checkDestination(const core::ImageMeta &meta, pvd::PixelFormat format,
                                                          std::size_t dstSize, std::uint32_t pitchBytes);
        /// Describes `dst` to libavif: 8 bits per channel BGR/BGRA, straight alpha, automatic chroma
        /// upsampling, `maxThreads` conversion threads, `pitchBytes` per row.
        [[nodiscard]] avifRGBImage rgbTarget(const avifImage &image, pvd::PixelFormat format, std::span<std::byte> dst,
                                             std::uint32_t pitchBytes, int maxThreads);

    } // namespace detail

    class DecoderFactory;

    class Decoder final : public core::IDecoder
    {
      public:
        /// Passkey: only `DecoderFactory` can construct a `Decoder`, which guarantees that `decoder` is
        /// a non-null handle whose `avifDecoderParse` already succeeded.
        class Key
        {
            Key() = default;
            friend class DecoderFactory;
        };

        Decoder(Key, DecoderHandle decoder, core::ImageMeta meta, int maxThreads) noexcept;
        ~Decoder() override = default;

        Decoder(const Decoder &) = delete;
        Decoder &operator=(const Decoder &) = delete;
        Decoder(Decoder &&) = delete;
        Decoder &operator=(Decoder &&) = delete;

        [[nodiscard]] const core::ImageMeta &meta() const override;
        [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t frame) const override;
        [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                     std::span<std::byte> dst, std::uint32_t pitchBytes) override;

      private:
        DecoderHandle decoder_;
        core::ImageMeta meta_;
        int maxThreads_;
    };

    class DecoderFactory final : public core::IDecoderFactory
    {
      public:
        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        /// Parses `file` and returns a decoder over it. `file` must outlive the returned decoder:
        /// `avifDecoderSetIOMemory` installs a persistent memory reader and libavif keeps pointers into
        /// `file` for every later `avifDecoderNthImage` call. `core::FileSession` guarantees this by
        /// declaring its file data before the decoder.
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;
    };

} // namespace pvdkit::avif
