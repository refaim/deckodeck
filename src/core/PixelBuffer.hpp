#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "core/Error.hpp"

namespace avifpvd::core {

struct PixelView {
  std::span<const std::byte> pixels;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t bytesPerPixel;
  std::uint32_t pitchBytes;
};

class PixelBuffer {
public:
  [[nodiscard]] static Result<PixelBuffer> create(std::uint32_t width,
                                                  std::uint32_t height,
                                                  std::uint32_t bytesPerPixel,
                                                  std::uint64_t maxPixels);

  PixelBuffer(PixelBuffer &&) noexcept = default;
  PixelBuffer &operator=(PixelBuffer &&) noexcept = default;
  PixelBuffer(const PixelBuffer &) = delete;
  PixelBuffer &operator=(const PixelBuffer &) = delete;

  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;
  [[nodiscard]] std::uint32_t bytesPerPixel() const noexcept;
  [[nodiscard]] std::uint32_t pitchBytes() const noexcept;
  [[nodiscard]] std::span<std::byte> bytes() noexcept;
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
  [[nodiscard]] PixelView view() const noexcept;

private:
  PixelBuffer(std::unique_ptr<std::byte[]> bytes, std::size_t size,
              std::uint32_t width, std::uint32_t height,
              std::uint32_t bytesPerPixel, std::uint32_t pitchBytes) noexcept;

  std::unique_ptr<std::byte[]> bytes_;
  std::size_t size_;
  std::uint32_t width_;
  std::uint32_t height_;
  std::uint32_t bytesPerPixel_;
  std::uint32_t pitchBytes_;
};

} // namespace avifpvd::core
