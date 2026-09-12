#include <cstddef>
#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

#include "core/Narrow.hpp"

namespace avifpvd::core {
namespace {

TEST_CASE("narrow keeps a 64-bit count that fits the target type") {
  const auto exact = narrow<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), "count");
  REQUIRE(exact.has_value());
  CHECK(*exact == std::numeric_limits<std::uint32_t>::max());

  const auto small = narrow<std::uint8_t>(255, "count");
  REQUIRE(small.has_value());
  CHECK(*small == 255);
}

TEST_CASE("narrow refuses a 64-bit count the target type cannot hold as TooLarge") {
  const auto tooWide = narrow<std::uint32_t>(std::uint64_t{1} << 32, "pixel buffer size");
  REQUIRE_FALSE(tooWide.has_value());
  CHECK(tooWide.error().code == ErrorCode::TooLarge);
  CHECK(tooWide.error().detail == "pixel buffer size (4294967296) does not fit in 32 bits");

  const auto byte = narrow<std::uint8_t>(256, "count");
  REQUIRE_FALSE(byte.has_value());
  CHECK(byte.error().code == ErrorCode::TooLarge);
  CHECK(byte.error().detail == "count (256) does not fit in 8 bits");
}

TEST_CASE("narrow to size_t follows the address width of the build") {
  const auto largest = narrow<std::size_t>(std::numeric_limits<std::size_t>::max(), "file size");
  REQUIRE(largest.has_value());
  CHECK(*largest == std::numeric_limits<std::size_t>::max());

  // 8 GiB: addressable by a 64-bit process, never by a 32-bit one.
  const auto eightGiB = narrow<std::size_t>(std::uint64_t{1} << 33, "file size");
  if constexpr (sizeof(std::size_t) == 4) {
    REQUIRE_FALSE(eightGiB.has_value());
    CHECK(eightGiB.error().code == ErrorCode::TooLarge);
    CHECK(eightGiB.error().detail == "file size (8589934592) does not fit in 32 bits");
  } else {
    REQUIRE(eightGiB.has_value());
    CHECK(*eightGiB == std::uint64_t{1} << 33);
  }
}

} // namespace
} // namespace avifpvd::core
