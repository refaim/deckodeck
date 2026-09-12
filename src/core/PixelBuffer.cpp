#include "core/PixelBuffer.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace avifpvd::core {

namespace {

Error tooLarge(const std::string_view detail) {
  return Error{ErrorCode::TooLarge, std::string{detail}};
}

} // namespace

Result<PixelBuffer> PixelBuffer::create(const std::uint32_t width,
                                        const std::uint32_t height,
                                        const std::uint32_t bytesPerPixel,
                                        const std::uint64_t maxPixels) {
  const auto pixelCount = static_cast<std::uint64_t>(width) * height;
  if (pixelCount > maxPixels) {
    return std::unexpected(
        tooLarge("pixel count exceeds the configured limit"));
  }

  const auto pitch = static_cast<std::uint64_t>(width) * bytesPerPixel;
  if (height != 0 &&
      pitch > std::numeric_limits<std::uint64_t>::max() / height) {
    return std::unexpected(tooLarge("pixel buffer size overflows"));
  }
  if (pitch > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(tooLarge("pixel pitch cannot be represented"));
  }

  const auto size = pitch * height;
  auto bytes = std::make_unique_for_overwrite<std::byte[]>(
      static_cast<std::size_t>(size));
  return PixelBuffer{
      std::move(bytes), static_cast<std::size_t>(size),   width, height,
      bytesPerPixel,    static_cast<std::uint32_t>(pitch)};
}

PixelBuffer::PixelBuffer(std::unique_ptr<std::byte[]> bytes,
                         const std::size_t size, const std::uint32_t width,
                         const std::uint32_t height,
                         const std::uint32_t bytesPerPixel,
                         const std::uint32_t pitchBytes) noexcept
    : bytes_(std::move(bytes)), size_(size), width_(width), height_(height),
      bytesPerPixel_(bytesPerPixel), pitchBytes_(pitchBytes) {}

std::uint32_t PixelBuffer::width() const noexcept { return width_; }

std::uint32_t PixelBuffer::height() const noexcept { return height_; }

std::uint32_t PixelBuffer::bytesPerPixel() const noexcept {
  return bytesPerPixel_;
}

std::uint32_t PixelBuffer::pitchBytes() const noexcept { return pitchBytes_; }

std::span<std::byte> PixelBuffer::bytes() noexcept {
  return {bytes_.get(), size_};
}

std::span<const std::byte> PixelBuffer::bytes() const noexcept {
  return {bytes_.get(), size_};
}

PixelView PixelBuffer::view() const noexcept {
  return PixelView{bytes(), width_, height_, bytesPerPixel_, pitchBytes_};
}

} // namespace avifpvd::core
