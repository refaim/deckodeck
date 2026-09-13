#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "core/Error.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::core
{

    /// Identifies the decoded image chroma subsampling.
    enum class ChromaFormat : std::uint8_t
    {
        Yuv444,
        Yuv422,
        Yuv420,
        Yuv400
    };

    /// Holds ISO/IEC 23091-2 colour signalling.
    struct Cicp
    {
        std::uint16_t primaries, transfer, matrix;
        bool fullRange;
    };

    /// Defines a crop rectangle in coded-image pixels.
    struct CropRect
    {
        std::uint32_t x, y, width, height;
    };

    /// Names the visual effect of a mirror transform (HEIF/AVIF `imir`).
    enum class MirrorAxis : std::uint8_t
    {
        TopBottom,
        LeftRight
    };

    /// Holds normalized transformative properties (HEIF/AVIF `clap`, `irot`, `imir`). Every field at
    /// its default means "none": formats without transforms leave the struct empty.
    struct Transforms
    {
        std::optional<CropRect> clap;
        std::uint8_t irotAngle = 0;
        std::optional<MirrorAxis> imir;
    };

    /// Describes a parsed image independently of the decoding library. `chroma` and `cicp` are the
    /// ISO/IEC 23091-2 signalling of the coded samples: RGB formats report `Yuv444` with
    /// `cicp.matrix = 0` (identity) and `cicp.fullRange = true`, which is how CICP itself spells RGB
    /// (sRGB PNG: `{1, 13, 0, true}`; unknown colour: `{2, 2, 0, true}`); greyscale is `Yuv400`.
    struct ImageMeta
    {
        std::uint32_t width = 0, height = 0;
        std::uint8_t depth = 0;
        ChromaFormat chroma = ChromaFormat::Yuv444;
        bool hasAlpha = false, alphaPremultiplied = false;
        Cicp cicp{};
        std::uint32_t frameCount = 0;
        bool animated = false;
        Transforms transforms;
        bool hasIcc = false, hasExif = false, hasXmp = false;
        bool indexed = false;    ///< Source stores palette indices; `depth` is the index width.
        bool interlaced = false; ///< Source is stored progressively (for example, PNG Adam7).
        /// HDR mastering/content peak in cd/m2 when the container exposes one.
        std::optional<float> masteringPeakNits{};
        std::uint8_t exifOrientation = 0; ///< 0 = absent/ignored; 1..8 use the EXIF orientation values.
    };

    /// Holds the display duration of one decoded frame.
    struct FrameTiming
    {
        std::uint32_t durationMs;
    };

    /// Configures decoder resource and validation limits.
    struct DecoderOptions
    {
        unsigned maxThreads = 0;
        bool strict = false;
        std::uint64_t maxPixels = 0;
        std::uint32_t maxDimension = 0;
        /// When the source has more than 8 bits per sample, preserve it in Bgra64 instead of
        /// reducing it to 8-bit output. Kept last and defaulted for aggregate callers.
        bool deepOutput = false;
    };

    /// Decodes frames from one parsed file.
    class IDecoder
    {
      public:
        virtual ~IDecoder() = default;
        [[nodiscard]] virtual const ImageMeta &meta() const = 0;
        /// Views ICC bytes owned by this decoder for its lifetime; empty means no profile.
        [[nodiscard]] virtual std::span<const std::byte> iccProfile() const = 0;
        [[nodiscard]] virtual Result<FrameTiming> frameTiming(std::uint32_t frame) const = 0;
        [[nodiscard]] virtual Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                       std::span<std::byte> dst, std::uint32_t pitchBytes) = 0;
    };

    /// Recognises the plugin's format by its signature and creates parsed decoder instances.
    class IDecoderFactory
    {
      public:
        virtual ~IDecoderFactory() = default;
        [[nodiscard]] virtual bool recognises(std::span<const std::byte> head) const = 0;
        [[nodiscard]] virtual Result<std::unique_ptr<IDecoder>> create(std::span<const std::byte> file,
                                                                       const DecoderOptions &) = 0;
    };

} // namespace pvdkit::core
