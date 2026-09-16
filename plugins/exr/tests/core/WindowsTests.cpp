#include <cstdint>

#include <doctest/doctest.h>

#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "core/Windows.hpp"

namespace pvdkit::exr
{
    namespace
    {

        constexpr core::DecoderOptions kOptions{1, false, 1'000'000, 4'000};

        TEST_CASE("equal display and data windows overlap completely")
        {
            const auto result = layout(Box{0, 0, 399, 299}, Box{0, 0, 399, 299}, kOptions);
            REQUIRE(result.has_value());
            CHECK(result->width == 400);
            CHECK(result->height == 300);
            REQUIRE(result->overlap.has_value());
            CHECK(result->overlap->xMin == 0);
            CHECK(result->overlap->yMin == 0);
            CHECK(result->overlap->xMax == 399);
            CHECK(result->overlap->yMax == 299);
        }

        TEST_CASE("the overlap is the intersection in absolute coordinates whichever window is larger")
        {
            // Data larger than display (t07): cropped to the display window.
            const auto cropped = layout(Box{-40, -40, 440, 330}, Box{0, 0, 399, 299}, kOptions);
            REQUIRE(cropped.has_value());
            CHECK(cropped->width == 481);
            CHECK(cropped->height == 371);
            CHECK(cropped->originX == -40);
            CHECK(cropped->originY == -40);
            REQUIRE(cropped->overlap.has_value());
            CHECK(cropped->overlap->xMin == 0);
            CHECK(cropped->overlap->yMax == 299);

            // Data inside display: padded around it.
            const auto padded = layout(Box{0, 0, 15, 15}, Box{4, 6, 9, 11}, kOptions);
            REQUIRE(padded.has_value());
            REQUIRE(padded->overlap.has_value());
            CHECK(padded->overlap->xMin == 4);
            CHECK(padded->overlap->yMin == 6);
            CHECK(padded->overlap->xMax == 9);
            CHECK(padded->overlap->yMax == 11);

            // One pixel in common (t13 / t14).
            const auto corner = layout(Box{399, 299, 499, 399}, Box{0, 0, 399, 299}, kOptions);
            REQUIRE(corner.has_value());
            REQUIRE(corner->overlap.has_value());
            CHECK(corner->overlap->xMin == 399);
            CHECK(corner->overlap->xMax == 399);
            CHECK(corner->overlap->yMin == 299);
            CHECK(corner->overlap->yMax == 299);
        }

        TEST_CASE("disjoint windows leave no overlap in either axis")
        {
            const auto right = layout(Box{400, 0, 599, 299}, Box{0, 0, 399, 299}, kOptions);
            REQUIRE(right.has_value());
            CHECK_FALSE(right->overlap.has_value());
            const auto below = layout(Box{0, 300, 399, 599}, Box{0, 0, 399, 299}, kOptions);
            REQUIRE(below.has_value());
            CHECK_FALSE(below->overlap.has_value());
        }

        TEST_CASE("the data window's area is bounded for the parts that are read whole")
        {
            // Luminance/chroma parts are reconstructed over the whole data window (DESIGN.md 5),
            // so their data area is held to the pixel limit like a display window; the check
            // runs before any allocation.
            constexpr core::DecoderOptions small{1, false, 100, 20};
            CHECK(checkDataArea(Box{0, 0, 9, 9}, small).has_value());
            CHECK(checkDataArea(Box{-5, -5, 4, 4}, small).has_value());
            const auto tooMany = checkDataArea(Box{0, 0, 10, 9}, small);
            REQUIRE_FALSE(tooMany.has_value());
            CHECK(tooMany.error().code == core::ErrorCode::TooLarge);
            CHECK(tooMany.error().detail.find("data window") != std::string::npos);
            const auto huge =
                checkDataArea(Box{-2'000'000'000, -2'000'000'000, 2'000'000'000, 2'000'000'000}, kOptions);
            REQUIRE_FALSE(huge.has_value());
            CHECK(huge.error().code == core::ErrorCode::TooLarge);
        }

        TEST_CASE("inverted or degenerate windows are malformed")
        {
            for (const auto &[display, data] :
                 {std::pair{Box{10, 0, 9, 9}, Box{0, 0, 9, 9}}, std::pair{Box{0, 10, 9, 9}, Box{0, 0, 9, 9}},
                  std::pair{Box{0, 0, 9, 9}, Box{10, 0, 9, 9}}, std::pair{Box{0, 0, 9, 9}, Box{0, 10, 9, 9}}}) {
                const auto result = layout(display, data, kOptions);
                REQUIRE_FALSE(result.has_value());
                CHECK(result.error().code == core::ErrorCode::ParseFailed);
            }
        }

        TEST_CASE("the display window is checked against the limits before anything is allocated")
        {
            constexpr core::DecoderOptions small{1, false, 100, 20};
            // 10x10 = 100 pixels fits exactly; 11x10 does not; 21 wide exceeds the dimension limit.
            CHECK(layout(Box{0, 0, 9, 9}, Box{0, 0, 9, 9}, small).has_value());
            const auto tooMany = layout(Box{0, 0, 10, 9}, Box{0, 0, 9, 9}, small);
            REQUIRE_FALSE(tooMany.has_value());
            CHECK(tooMany.error().code == core::ErrorCode::TooLarge);
            const auto tooWide = layout(Box{0, 0, 20, 0}, Box{0, 0, 0, 0}, small);
            REQUIRE_FALSE(tooWide.has_value());
            CHECK(tooWide.error().code == core::ErrorCode::TooLarge);
            const auto tooTall = layout(Box{0, 0, 0, 20}, Box{0, 0, 0, 0}, small);
            REQUIRE_FALSE(tooTall.has_value());
            CHECK(tooTall.error().code == core::ErrorCode::TooLarge);

            // The data window's sides are bounded by the dimension limit too (it sizes the chunk
            // scratch), while its area is not: a huge data window with a tiny display is cropped.
            const auto wideData = layout(Box{0, 0, 1, 1}, Box{0, 0, 21, 1}, small);
            REQUIRE_FALSE(wideData.has_value());
            CHECK(wideData.error().code == core::ErrorCode::TooLarge);
            const auto tallData = layout(Box{0, 0, 1, 1}, Box{0, 0, 1, 21}, small);
            REQUIRE_FALSE(tallData.has_value());
            CHECK(tallData.error().code == core::ErrorCode::TooLarge);
            CHECK(layout(Box{0, 0, 1, 1}, Box{-5, -5, 14, 14}, small).has_value());

            // Full 32-bit coordinate ranges do not overflow the arithmetic.
            const auto huge =
                layout(Box{-2'000'000'000, -2'000'000'000, 2'000'000'000, 2'000'000'000}, Box{0, 0, 0, 0}, kOptions);
            REQUIRE_FALSE(huge.has_value());
            CHECK(huge.error().code == core::ErrorCode::TooLarge);
        }

    } // namespace
} // namespace pvdkit::exr
