#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "core/Encode.hpp"
#include "core/PqTables.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::exr
{
    namespace
    {

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInf = std::numeric_limits<float>::infinity();

        const PqCodeTables &tables()
        {
            static const PqCodeTables instance;
            return instance;
        }

        std::uint16_t referenceCode(const double nits)
        {
            // SMPTE ST 2084 inverse EOTF in double precision, independent of the shared float path.
            constexpr double m1 = 2610.0 / 16384.0;
            constexpr double m2 = 2523.0 / 4096.0 * 128.0;
            constexpr double c1 = 3424.0 / 4096.0;
            constexpr double c2 = 2413.0 / 4096.0 * 32.0;
            constexpr double c3 = 2392.0 / 4096.0 * 32.0;
            const double y = std::pow(std::min(nits, 10'000.0) / 10'000.0, m1);
            const double code = std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
            return static_cast<std::uint16_t>(std::lround(code * 65'535.0));
        }

        TEST_CASE("one unit of scene light is the white luminance or 100 nit by default")
        {
            CHECK(nitsPerUnit(std::nullopt) == 100.0F);
            CHECK(nitsPerUnit(250.0F) == 250.0F);
            CHECK(nitsPerUnit(0.0F) == 100.0F);
            CHECK(nitsPerUnit(-5.0F) == 100.0F);
            CHECK(nitsPerUnit(kNaN) == 100.0F);
            CHECK(nitsPerUnit(kInf) == 100.0F);
        }

        TEST_CASE("PQ codes are the 16-bit ST 2084 codes of the sanitized nits")
        {
            CHECK(pqCode(0.0F) == 0);
            CHECK(pqCode(-1.0F) == 0);
            CHECK(pqCode(-kInf) == 0);
            CHECK(pqCode(kNaN) == 0);
            CHECK(pqCode(kInf) == 65'535);
            CHECK(pqCode(20'000.0F) == 65'535);
            CHECK(pqCode(10'000.0F) == 65'535);
            for (const double nits : {0.005, 0.1, 1.0, 18.0, 100.0, 203.0, 1'000.0, 4'000.0, 9'999.0}) {
                CAPTURE(nits);
                CHECK(pqCode(static_cast<float>(nits)) == referenceCode(static_cast<double>(static_cast<float>(nits))));
            }
        }

        TEST_CASE("the tone-mapping peak is the 99.99th percentile luminance clamped to 100..10000 nit")
        {
            LuminanceHistogram dim;
            dim.add(pqCode(50.0F));
            CHECK(dim.count() == 1);
            CHECK(peakNits(dim, 1) == 100.0F);

            // 9999 dim pixels and one bright one: the percentile falls on a dim pixel.
            LuminanceHistogram oneBright;
            for (int index = 0; index < 9'999; ++index) {
                oneBright.add(pqCode(50.0F));
            }
            oneBright.add(pqCode(5'000.0F));
            CHECK(peakNits(oneBright, 10'000) == 100.0F);

            // Two bright pixels out of 10000: the percentile reaches them.
            LuminanceHistogram twoBright;
            for (int index = 0; index < 9'998; ++index) {
                twoBright.add(pqCode(50.0F));
            }
            twoBright.add(pqCode(5'000.0F));
            twoBright.add(pqCode(5'000.0F));
            CHECK(peakNits(twoBright, 10'000) == doctest::Approx(5'000.0F).epsilon(2.0e-3));

            // Pixels outside the data window count as black: five bright pixels of a 10000-pixel
            // display window are all above the percentile.
            LuminanceHistogram sparse;
            for (int index = 0; index < 5; ++index) {
                sparse.add(pqCode(2'000.0F));
            }
            CHECK(peakNits(sparse, 10'000) == doctest::Approx(2'000.0F).epsilon(2.0e-3));
            LuminanceHistogram sparser;
            sparser.add(pqCode(2'000.0F));
            CHECK(peakNits(sparser, 10'000) == 100.0F);

            // Nothing decoded at all (no overlap) and beyond-PQ brightness.
            const LuminanceHistogram empty;
            CHECK(peakNits(empty, 10'000) == 100.0F);
            LuminanceHistogram blinding;
            blinding.add(pqCode(50'000.0F));
            CHECK(peakNits(blinding, 1) == 10'000.0F);
            LuminanceHistogram mid;
            mid.add(pqCode(1'000.0F));
            CHECK(peakNits(mid, 1) == doctest::Approx(1'000.0F).epsilon(2.0e-3));
        }

        TEST_CASE("pixels are encoded as straight-alpha BGRA PQ codes and their luminance is histogrammed")
        {
            const EncodeParams opaque{100.0F, false, false, {0.2126F, 0.7152F, 0.0722F}, tables()};
            const std::array<float, 8> grey{0.18F, 0.18F, 0.18F, 0.0F, 1.0F, 0.5F, 0.25F, 0.0F};
            std::array<std::uint16_t, 8> out{};
            LuminanceHistogram histogram;
            encodePixels(grey, opaque, out, histogram);
            CHECK(out[0] == pqCode(18.0F));
            CHECK(out[1] == pqCode(18.0F));
            CHECK(out[2] == pqCode(18.0F));
            CHECK(out[3] == 65'535);
            CHECK(out[4] == pqCode(25.0F));  // B
            CHECK(out[5] == pqCode(50.0F));  // G
            CHECK(out[6] == pqCode(100.0F)); // R
            CHECK(out[7] == 65'535);
            CHECK(histogram.count() == 2);
            // Luminance of the second pixel: 0.2126*100 + 0.7152*50 + 0.0722*25 = 58.825 nit.
            LuminanceHistogram expected;
            expected.add(pqCode(18.0F));
            expected.add(pqCode(58.825F));
            CHECK(histogram == expected);
        }

        TEST_CASE("associated alpha is divided out and alpha is clamped and sanitized")
        {
            const EncodeParams withAlpha{100.0F, true, false, {0.2126F, 0.7152F, 0.0722F}, tables()};
            const std::array<float, 28> pixels{
                0.09F, 0.045F, 0.0225F, 0.5F,  // half-covered: straight (0.18, 0.09, 0.045)
                0.4F,  0.2F,   0.1F,    0.0F,  // zero alpha keeps its colour (additive light)
                0.4F,  0.2F,   0.1F,    2.0F,  // alpha above one is clamped, no division
                0.4F,  0.2F,   0.1F,    kNaN,  // NaN alpha is transparent
                0.4F,  0.2F,   0.1F,    -1.0F, // negative alpha is transparent
                0.4F,  0.2F,   0.1F,    kInf,  // infinite alpha is opaque
                0.0F,  0.0F,   0.0F,    0.1F,  // 6553.5 rounds up, as lround does
            };
            std::array<std::uint16_t, 28> out{};
            LuminanceHistogram histogram;
            encodePixels(pixels, withAlpha, out, histogram);
            CHECK(out[0] == pqCode(4.5F));
            CHECK(out[1] == pqCode(9.0F));
            CHECK(out[2] == pqCode(18.0F));
            CHECK(out[3] == 32'768);
            CHECK(out[4] == pqCode(10.0F));
            CHECK(out[6] == pqCode(40.0F));
            CHECK(out[7] == 0);
            CHECK(out[10] == pqCode(40.0F));
            CHECK(out[11] == 65'535);
            CHECK(out[15] == 0);
            CHECK(out[19] == 0);
            CHECK(out[23] == 65'535);
            CHECK(out[27] == 6'554);
            CHECK(histogram.count() == 7);
        }

        TEST_CASE("non-finite and negative colour is sanitized and the white luminance scales the nits")
        {
            const EncodeParams bright{250.0F, false, false, {0.2126F, 0.7152F, 0.0722F}, tables()};
            const std::array<float, 12> pixels{kNaN, -1.0F, kInf,  0.0F,  1.0F, 1.0F,
                                               1.0F, 0.0F,  -kInf, 40.0F, 0.0F, 0.0F};
            std::array<std::uint16_t, 12> out{};
            LuminanceHistogram histogram;
            encodePixels(pixels, bright, out, histogram);
            CHECK(out[0] == 65'535); // B = +inf
            CHECK(out[1] == 0);      // G = -1
            CHECK(out[2] == 0);      // R = NaN
            CHECK(out[4] == pqCode(250.0F));
            CHECK(out[5] == pqCode(250.0F));
            CHECK(out[6] == pqCode(250.0F));
            CHECK(out[8] == 0);
            CHECK(out[9] == pqCode(10'000.0F)); // G = 40 * 250 = 10000 nit exactly at the PQ peak
            CHECK(out[10] == 0);
            // The luminance of a pixel with a NaN and an infinite channel is finite or saturated,
            // never NaN: the histogram counts every pixel.
            CHECK(histogram.count() == 3);
            CHECK(peakNits(histogram, 3) == 10'000.0F);

            // Wide-gamut luminance weights can turn a saturated colour negative; that counts as black.
            const EncodeParams ap0{100.0F, false, false, {0.3439664F, 0.7281661F, -0.0721325F}, tables()};
            const std::array<float, 4> blue{0.0F, 0.0F, 1.0F, 0.0F};
            std::array<std::uint16_t, 4> blueOut{};
            LuminanceHistogram blueHistogram;
            encodePixels(blue, ap0, blueOut, blueHistogram);
            LuminanceHistogram black;
            black.add(0);
            CHECK(blueHistogram == black);
        }

        TEST_CASE("a grey source feeds its single channel to all three outputs")
        {
            const EncodeParams grey{100.0F, true, true, {0.2126F, 0.7152F, 0.0722F}, tables()};
            const std::array<float, 8> pixels{0.5F, 99.0F, 99.0F, 1.0F, 0.09F, 0.0F, 0.0F, 0.5F};
            std::array<std::uint16_t, 8> out{};
            LuminanceHistogram histogram;
            encodePixels(pixels, grey, out, histogram);
            CHECK(out[0] == pqCode(50.0F));
            CHECK(out[1] == pqCode(50.0F));
            CHECK(out[2] == pqCode(50.0F));
            CHECK(out[3] == 65'535);
            CHECK(out[4] == pqCode(18.0F));
            CHECK(out[6] == pqCode(18.0F));
            CHECK(out[7] == 32'768);
            LuminanceHistogram expected;
            expected.add(pqCode(50.0F));
            expected.add(pqCode(18.0F));
            CHECK(histogram == expected);
        }

    } // namespace
} // namespace pvdkit::exr
