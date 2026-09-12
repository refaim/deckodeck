#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "core/Error.hpp"
#include "pvd/Types.hpp"

namespace avifpvd::core {

/// Identifies the decoded image chroma subsampling.
enum class ChromaFormat { Yuv444, Yuv422, Yuv420, Yuv400 };

/// Holds ISO/IEC 23091-2 colour signalling.
struct Cicp {
  std::uint16_t primaries, transfer, matrix;
  bool fullRange;
};

/// Defines a crop rectangle in coded-image pixels.
struct CropRect {
  std::uint32_t x, y, width, height;
};

/// Names the visual effect of an AVIF mirror transform.
enum class MirrorAxis { TopBottom, LeftRight };

/// Holds normalized AVIF transformative properties.
struct Transforms {
  std::optional<CropRect> clap;
  std::uint8_t irotAngle = 0;
  std::optional<MirrorAxis> imir;
};

/// Describes parsed AVIF image metadata independently of libavif.
struct ImageMeta {
  std::uint32_t width, height;
  std::uint8_t depth;
  ChromaFormat chroma;
  bool hasAlpha, alphaPremultiplied;
  Cicp cicp;
  std::uint32_t frameCount;
  bool animated;
  Transforms transforms;
  bool hasIcc, hasExif, hasXmp;
};

/// Holds the display duration of one decoded frame.
struct FrameTiming {
  std::uint32_t durationMs;
};

/// Configures decoder resource and validation limits.
struct DecoderOptions {
  unsigned maxThreads;
  bool strict;
  std::uint64_t maxPixels;
  std::uint32_t maxDimension;
};

/// Decodes frames from one parsed AVIF file.
class IDecoder {
 public:
  virtual ~IDecoder() = default;
  [[nodiscard]] virtual const ImageMeta& meta() const = 0;
  [[nodiscard]] virtual Result<FrameTiming> frameTiming(std::uint32_t frame) const = 0;
  [[nodiscard]] virtual Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                 std::span<std::byte> dst,
                                                 std::uint32_t pitchBytes) = 0;
};

/// Recognizes AVIF input and creates parsed decoder instances.
class IDecoderFactory {
 public:
  virtual ~IDecoderFactory() = default;
  [[nodiscard]] virtual bool looksLikeAvif(std::span<const std::byte> head) const = 0;
  [[nodiscard]] virtual Result<std::unique_ptr<IDecoder>> create(std::span<const std::byte> file,
                                                                 const DecoderOptions&) = 0;
};

}  // namespace avifpvd::core
