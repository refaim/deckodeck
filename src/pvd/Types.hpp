#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace pvdkit::pvd {

/// Describes this decoder plugin to the PictureView host.
struct PluginInfo {
  std::uint32_t priority;
  std::string name, version, comments;
};

/// Describes an opened image to the PictureView host.
struct ImageInfo {
  std::uint32_t pageCount;
  bool animated;
  std::string formatName, compression, comments;
};

/// Describes one page or animation frame.
struct PageInfo {
  std::uint32_t width, height, bitsPerPixel, frameTimeMs;
};

/// Selects the byte layout returned to PictureView.
enum class PixelFormat { Bgr24, Bgra32 };

/// Views decoded pixel memory owned by the file session.
struct DecodedPage {
  std::span<const std::byte> pixels;
  std::uint32_t bitsPerPixel;
  std::uint32_t pitchBytes;
};

/// Carries the host-provided inputs needed to open an image.
struct OpenRequest {
  std::string_view utf8FileName;
  std::uint64_t fileSize;
  std::span<const std::byte> head;
};

/// Wraps the optional PictureView progress callback.
class Progress {
 public:
  /// Defines the C++ progress callback signature.
  using Fn = std::function<bool(std::uint32_t step, std::uint32_t steps)>;

  Progress() = default;
  explicit Progress(Fn fn);
  [[nodiscard]] bool report(std::uint32_t step, std::uint32_t steps) const;

 private:
  Fn fn_;
};

}  // namespace pvdkit::pvd
