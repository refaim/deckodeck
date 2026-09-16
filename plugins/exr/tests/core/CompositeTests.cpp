#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "core/Composite.hpp"
#include "core/Windows.hpp"

namespace pvdkit::exr
{
    namespace
    {

        std::uint16_t sample(const std::span<const std::byte> dst, const std::uint32_t pitch, const std::uint32_t x,
                             const std::uint32_t y, const std::size_t channel)
        {
            const auto offset = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * 8 + channel * 2;
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(dst[offset]) |
                                              (std::to_integer<std::uint8_t>(dst[offset + 1]) << 8U));
        }

        // A 2x2 overlap whose pixel (x, y) carries codes (1000 + x + 10 y, 2000 + ..., 3000 + ..., 4000 + ...).
        std::vector<std::uint16_t> overlapPixels()
        {
            std::vector<std::uint16_t> pixels;
            for (std::uint16_t y = 0; y < 2; ++y) {
                for (std::uint16_t x = 0; x < 2; ++x) {
                    for (std::uint16_t channel = 0; channel < 4; ++channel) {
                        pixels.push_back(static_cast<std::uint16_t>(1'000 * (channel + 1) + x + 10 * y));
                    }
                }
            }
            return pixels;
        }

        TEST_CASE("the overlap lands at its display position and the rest is black")
        {
            // Display window (-1,-1)-(2,2) = 4x4 at origin (-1,-1); overlap (0,1)-(1,2).
            const Layout layout{4, 4, -1, -1, Box{0, 1, 1, 2}};
            const auto pixels = overlapPixels();
            constexpr std::uint32_t kPitch = 4 * 8 + 8; // padded rows are honoured
            std::vector<std::byte> dst(static_cast<std::size_t>(kPitch) * 4, std::byte{0xAB});

            composite(layout, pixels, false, dst, kPitch);

            // Overlap pixel (0,1) is display column 1, row 2.
            CHECK(sample(dst, kPitch, 1, 2, 0) == 1'000);
            CHECK(sample(dst, kPitch, 1, 2, 1) == 2'000);
            CHECK(sample(dst, kPitch, 1, 2, 2) == 3'000);
            CHECK(sample(dst, kPitch, 1, 2, 3) == 4'000);
            CHECK(sample(dst, kPitch, 2, 3, 0) == 1'011);
            CHECK(sample(dst, kPitch, 2, 3, 3) == 4'011);
            // Everything else is opaque black without alpha.
            for (const auto [x, y] : {std::pair{0U, 0U}, std::pair{3U, 3U}, std::pair{0U, 2U}, std::pair{3U, 2U},
                                      std::pair{1U, 0U}, std::pair{1U, 1U}}) {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(sample(dst, kPitch, x, y, 0) == 0);
                CHECK(sample(dst, kPitch, x, y, 1) == 0);
                CHECK(sample(dst, kPitch, x, y, 2) == 0);
                CHECK(sample(dst, kPitch, x, y, 3) == 65'535);
            }
            // The padding bytes beyond each row are untouched.
            CHECK(dst[4 * 8] == std::byte{0xAB});
        }

        TEST_CASE("with alpha the surroundings are transparent black and no overlap fills everything")
        {
            const Layout layout{3, 2, 0, 0, Box{2, 1, 2, 1}};
            const std::vector<std::uint16_t> pixel{7, 8, 9, 10};
            std::vector<std::byte> dst(3 * 2 * 8, std::byte{0xAB});
            composite(layout, pixel, true, dst, 3 * 8);
            CHECK(sample(dst, 24, 2, 1, 0) == 7);
            CHECK(sample(dst, 24, 2, 1, 3) == 10);
            CHECK(sample(dst, 24, 0, 0, 3) == 0);
            CHECK(sample(dst, 24, 1, 1, 3) == 0);
            CHECK(sample(dst, 24, 2, 0, 0) == 0);

            const Layout disjoint{2, 2, 5, 5, std::nullopt};
            std::vector<std::byte> empty(2 * 2 * 8, std::byte{0xAB});
            composite(disjoint, {}, false, empty, 16);
            for (std::uint32_t y = 0; y < 2; ++y) {
                for (std::uint32_t x = 0; x < 2; ++x) {
                    CHECK(sample(empty, 16, x, y, 0) == 0);
                    CHECK(sample(empty, 16, x, y, 3) == 65'535);
                }
            }
        }

    } // namespace
} // namespace pvdkit::exr
