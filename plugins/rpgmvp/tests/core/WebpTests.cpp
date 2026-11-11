#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <doctest/doctest.h>

#include "core/Webp.hpp"

namespace
{
    using pvdkit::rpgmvp::webp::detectWebp;
    using pvdkit::rpgmvp::webp::reconstructedWebpHeader;
    using pvdkit::rpgmvp::webp::WebpKind;

    std::array<std::byte, 49> encryptedHeader()
    {
        std::array<std::byte, 49> bytes{};
        constexpr std::array signature{std::byte{0x52}, std::byte{0x50}, std::byte{0x47}, std::byte{0x4d},
                                       std::byte{0x56}, std::byte{0},    std::byte{0},    std::byte{0}};
        std::ranges::copy(signature, bytes.begin());
        return bytes;
    }
} // namespace

TEST_CASE("encrypted WebP detection uses the unchanged chunk payload rather than the encrypted prefix")
{
    auto bytes = encryptedHeader();
    bytes[32] = std::byte{5};
    bytes[36] = std::byte{0x2f};
    CHECK(detectWebp(bytes) == WebpKind::Lossless);
    CHECK(detectWebp(std::span{bytes}.first(41)) == WebpKind::Lossless);
    std::ranges::fill(std::span{bytes}.subspan(8, 24), std::byte{0xa5});
    CHECK(detectWebp(bytes) == WebpKind::Lossless);

    bytes[36] = std::byte{0};
    bytes[39] = std::byte{0x9d};
    bytes[40] = std::byte{0x01};
    bytes[41] = std::byte{0x2a};
    CHECK(detectWebp(bytes) == WebpKind::Lossy);

    bytes = encryptedHeader();
    bytes[32] = std::byte{10};
    CHECK(detectWebp(bytes) == WebpKind::Extended);
    bytes[36] = std::byte{0x3e};
    CHECK(detectWebp(bytes) == WebpKind::Extended);
}

TEST_CASE("encrypted WebP detection rejects short input wrong magic and unsupported payload signatures")
{
    auto bytes = encryptedHeader();
    bytes[32] = std::byte{5};
    bytes[36] = std::byte{0x2f};
    CHECK_FALSE(detectWebp({}));
    CHECK_FALSE(detectWebp(std::span{bytes}.first(40)));
    for (std::size_t index = 0; index < 8; ++index) {
        auto broken = bytes;
        broken[index] ^= std::byte{0xff};
        CHECK_FALSE(detectWebp(broken));
    }
    bytes[36] = std::byte{0};
    CHECK_FALSE(detectWebp(bytes));
    bytes[39] = std::byte{0x9d};
    bytes[40] = std::byte{0x01};
    bytes[41] = std::byte{0x2a};
    CHECK_FALSE(detectWebp(std::span{bytes}.first(41)));
    for (const auto index : {39U, 40U, 41U}) {
        auto broken = bytes;
        broken[index] ^= std::byte{0xff};
        CHECK_FALSE(detectWebp(broken));
    }
    bytes = encryptedHeader();
    bytes[32] = std::byte{10};
    CHECK_FALSE(detectWebp(std::span{bytes}.first(41)));
    for (const auto index : {33U, 34U, 35U, 36U, 37U, 38U, 39U}) {
        auto broken = bytes;
        broken[index] = std::byte{0x80};
        CHECK_FALSE(detectWebp(broken));
    }
}

TEST_CASE("WebP reconstruction restores each RIFF FourCC and derives its little endian size")
{
    constexpr std::array kinds{WebpKind::Lossless, WebpKind::Lossy, WebpKind::Extended};
    constexpr std::array fourth{std::byte{'L'}, std::byte{' '}, std::byte{'X'}};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        auto result = reconstructedWebpHeader(kinds[index], 0x12345678ULL + 24ULL);
        REQUIRE(result);
        constexpr std::array prefix{std::byte{'R'},  std::byte{'I'},  std::byte{'F'},  std::byte{'F'}, std::byte{0x78},
                                    std::byte{0x56}, std::byte{0x34}, std::byte{0x12}, std::byte{'W'}, std::byte{'E'},
                                    std::byte{'B'},  std::byte{'P'},  std::byte{'V'},  std::byte{'P'}, std::byte{'8'}};
        CHECK(std::ranges::equal(std::span{*result}.first(15), prefix));
        CHECK((*result)[15] == fourth[index]);
    }
    CHECK(reconstructedWebpHeader(WebpKind::Lossless, 41));
    CHECK(
        reconstructedWebpHeader(WebpKind::Lossless, std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 24ULL));
    const auto shortFile = reconstructedWebpHeader(WebpKind::Lossless, 40);
    REQUIRE_FALSE(shortFile);
    CHECK(shortFile.error().code == pvdkit::core::ErrorCode::NotRecognised);
    const auto large =
        reconstructedWebpHeader(WebpKind::Lossless, std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 25ULL);
    REQUIRE_FALSE(large);
    CHECK(large.error().code == pvdkit::core::ErrorCode::TooLarge);
}
