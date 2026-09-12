#include <cstddef>
#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

#include "core/PixelBuffer.hpp"

namespace avifpvd::core {
namespace {

TEST_CASE(
    "PixelBuffer creates tightly packed writable storage and a const view") {
  auto created = PixelBuffer::create(2, 3, 4, 6);

  REQUIRE(created.has_value());
  auto &buffer = *created;
  CHECK(buffer.width() == 2);
  CHECK(buffer.height() == 3);
  CHECK(buffer.bytesPerPixel() == 4);
  CHECK(buffer.pitchBytes() == 8);
  REQUIRE(buffer.bytes().size() == 24);

  buffer.bytes().front() = std::byte{0x2a};
  const auto view = buffer.view();
  CHECK(view.pixels.data() == buffer.bytes().data());
  CHECK(view.pixels.size() == 24);
  CHECK(view.pixels.front() == std::byte{0x2a});
  CHECK(view.width == 2);
  CHECK(view.height == 3);
  CHECK(view.bytesPerPixel == 4);
  CHECK(view.pitchBytes == 8);
}

TEST_CASE("PixelBuffer rejects dimensions over the pixel limit") {
  const auto created = PixelBuffer::create(3, 2, 4, 5);

  REQUIRE_FALSE(created.has_value());
  CHECK(created.error().code == ErrorCode::TooLarge);
}

TEST_CASE("PixelBuffer permits an empty image without overflowing") {
  const auto created = PixelBuffer::create(2, 0, 4, 0);

  REQUIRE(created.has_value());
  CHECK(created->bytes().empty());
  CHECK(created->pitchBytes() == 8);
}

TEST_CASE("PixelBuffer rejects a pitch that cannot be represented") {
  const auto created =
      PixelBuffer::create(std::numeric_limits<std::uint32_t>::max(), 1, 4,
                          std::numeric_limits<std::uint64_t>::max());

  REQUIRE_FALSE(created.has_value());
  CHECK(created.error().code == ErrorCode::TooLarge);
}

TEST_CASE("PixelBuffer rejects overflowing storage arithmetic") {
  const auto maximum = std::numeric_limits<std::uint32_t>::max();
  const auto created = PixelBuffer::create(
      maximum, maximum, maximum, std::numeric_limits<std::uint64_t>::max());

  REQUIRE_FALSE(created.has_value());
  CHECK(created.error().code == ErrorCode::TooLarge);
}

} // namespace
} // namespace avifpvd::core
