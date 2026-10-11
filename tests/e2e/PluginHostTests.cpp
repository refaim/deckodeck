#include <array>

#include <doctest/doctest.h>

#include "PluginHost.hpp"
#include "RgbOutputChecks.hpp"

namespace pvdkit::e2e
{

    TEST_CASE("the host accepts supported decoded layouts with enough row pitch")
    {
        CHECK(isSupportedDecodeLayout(2, 24, 6));
        CHECK(isSupportedDecodeLayout(2, 32, 8));
        CHECK(isSupportedDecodeLayout(2, 64, 16));
        CHECK(isSupportedDecodeLayout(2, 64, 17));
    }

    TEST_CASE("the host accepts native RGB wire codes using their stored pixel sizes")
    {
        CHECK(isSupportedDecodeLayout(2, 25, 6));
        CHECK(isSupportedDecodeLayout(2, 33, 8));
        CHECK(isSupportedDecodeLayout(2, 65, 16));
        CHECK(isSupportedDecodeLayout(2, 65, 17));
    }

    TEST_CASE("the host accepts native RGB48 wire code with six bytes per pixel")
    {
        CHECK(isSupportedDecodeLayout(2, 49, 12));
        CHECK(isSupportedDecodeLayout(2, 49, 13));
    }

    TEST_CASE("RGB48 semantic comparison drops opaque alpha and preserves both bytes of each sample")
    {
        const pvdInfoPage page{2, 2, 48, 0};
        std::array<BYTE, 36> legacyPixels{1,  2,  3,  4,  5,  6,  255, 255, 7,  8,  9,  10, 11, 12, 255, 255, 99, 99,
                                          13, 14, 15, 16, 17, 18, 255, 255, 19, 20, 21, 22, 23, 24, 255, 255, 99, 99};
        std::array<BYTE, 28> nativePixels{5,  6,  3,  4,  1,  2,  11, 12, 9,  10, 7,  8,  88, 88,
                                          17, 18, 15, 16, 13, 14, 23, 24, 21, 22, 19, 20, 88, 88};
        const DecodedPage legacy{page, {legacyPixels.data(), nullptr, 0, 64, 0, 18, nullptr, 0}};
        const DecodedPage native{page, {nativePixels.data(), nullptr, 0, 49, 0, 14, nullptr, 0}};
        CHECK(native.bytesPerPixel() == 6);
        CHECK(native.row(0).size() == 12);
        CHECK(native.row(1).front() == std::byte{17});
        rgbChecks::checkPixelOrderEquivalent(native, legacy);
    }

    TEST_CASE("semantic pixel comparison preserves unchanged legacy output for modules without native RGB")
    {
        const pvdInfoPage page{1, 1, 48, 0};
        std::array<BYTE, 8> pixels{1, 2, 3, 4, 5, 6, 255, 255};
        const DecodedPage legacy{page, {pixels.data(), nullptr, 0, 64, 0, 8, nullptr, 0}};
        rgbChecks::checkPixelOrderEquivalent(legacy, legacy);
    }

    TEST_CASE("the host rejects invalid native RGB pitches")
    {
        CHECK_FALSE(isSupportedDecodeLayout(2, 25, 5));
        CHECK_FALSE(isSupportedDecodeLayout(2, 25, 7));
        CHECK_FALSE(isSupportedDecodeLayout(2, 33, 7));
        CHECK_FALSE(isSupportedDecodeLayout(2, 33, 9));
        CHECK_FALSE(isSupportedDecodeLayout(2, 65, 15));
        CHECK_FALSE(isSupportedDecodeLayout(2, 65, 0));
        CHECK_FALSE(isSupportedDecodeLayout(2, 65, -16));
        CHECK_FALSE(isSupportedDecodeLayout(2, 49, 11));
        CHECK_FALSE(isSupportedDecodeLayout(2, 49, 0));
        CHECK_FALSE(isSupportedDecodeLayout(2, 49, -12));
    }

    TEST_CASE("the host rejects unsupported decoded layouts and undersized row pitch")
    {
        CHECK_FALSE(isSupportedDecodeLayout(2, 8, 2));
        CHECK_FALSE(isSupportedDecodeLayout(2, 24, 5));
        CHECK_FALSE(isSupportedDecodeLayout(2, 24, 7));
        CHECK_FALSE(isSupportedDecodeLayout(2, 32, 7));
        CHECK_FALSE(isSupportedDecodeLayout(2, 32, 9));
        CHECK_FALSE(isSupportedDecodeLayout(2, 64, 15));
        CHECK_FALSE(isSupportedDecodeLayout(2, 64, 0));
        CHECK_FALSE(isSupportedDecodeLayout(2, 64, -16));
    }

} // namespace pvdkit::e2e
