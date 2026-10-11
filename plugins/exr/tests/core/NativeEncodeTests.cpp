#include <array>
#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

#include "core/Encode.hpp"

namespace pvdkit::exr
{
    TEST_CASE("native encoding writes compact RGB or straight RGBA without changing the luminance histogram")
    {
        const PqCodeTables tables;
        const std::array<float, 16> source{0.09F,
                                           0.045F,
                                           0.0225F,
                                           0.5F,
                                           0.4F,
                                           0.2F,
                                           0.1F,
                                           0.0F,
                                           std::numeric_limits<float>::quiet_NaN(),
                                           -1.0F,
                                           std::numeric_limits<float>::infinity(),
                                           2.0F,
                                           0.18F,
                                           99.0F,
                                           99.0F,
                                           1.0F};
        for (const bool alpha : {false, true}) {
            for (const bool grey : {false, true}) {
                CAPTURE(alpha);
                CAPTURE(grey);
                EncodeParams params{100.0F, alpha, grey, {0.2126F, 0.7152F, 0.0722F}, tables};
                std::array<std::uint16_t, 16> legacy{};
                LuminanceHistogram legacyHistogram;
                encodePixels(source, params, legacy, legacyHistogram);
                params.format = alpha ? pvd::PixelFormat::Rgba64 : pvd::PixelFormat::Rgb48;
                const std::size_t channels = alpha ? 4 : 3;
                std::array<std::uint16_t, 18> native;
                native.fill(12345);
                LuminanceHistogram nativeHistogram;
                encodePixels(source, params, native, nativeHistogram);
                for (std::size_t pixel = 0; pixel < 4; ++pixel) {
                    CHECK(native[pixel * channels] == legacy[pixel * 4 + 2]);
                    CHECK(native[pixel * channels + 1] == legacy[pixel * 4 + 1]);
                    CHECK(native[pixel * channels + 2] == legacy[pixel * 4]);
                    if (alpha) {
                        CHECK(native[pixel * channels + 3] == legacy[pixel * 4 + 3]);
                    }
                }
                CHECK(native[4 * channels] == 12345);
                CHECK(nativeHistogram == legacyHistogram);
            }
        }
    }
} // namespace pvdkit::exr
