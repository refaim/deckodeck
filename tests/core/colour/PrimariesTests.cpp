#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

#include "core/colour/Primaries.hpp"

namespace pvdkit::core::colour::Primaries
{
    namespace
    {

        void checkMatrix(const Matrix3 &actual, const Matrix3 &expected)
        {
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    CAPTURE(row);
                    CAPTURE(column);
                    CHECK(std::abs(actual.rows[row][column] - expected.rows[row][column]) < 1.0e-4F);
                }
            }
        }

        TEST_CASE("derived wide-gamut matrices match published references")
        {
            // ITU-R BT.2087, Table 2: BT.2020 linear RGB to BT.709 linear RGB.
            constexpr Matrix3 bt2020Reference{{{{1.660491F, -0.587641F, -0.072850F},
                                                {-0.124550F, 1.132900F, -0.008349F},
                                                {-0.018151F, -0.100579F, 1.118730F}}}};
            checkMatrix(toSrgb(9), bt2020Reference);

            // SMPTE EG 432-1 P3-D65 to BT.709/sRGB, derived through their common D65 white.
            constexpr Matrix3 p3D65Reference{
                {{{1.224940F, -0.224940F, 0.0F}, {-0.042057F, 1.042057F, 0.0F}, {-0.019638F, -0.078636F, 1.098274F}}}};
            checkMatrix(toSrgb(12), p3D65Reference);
        }

        TEST_CASE("every supported primary set preserves its adapted white")
        {
            constexpr std::array<std::uint16_t, 9> codes{1, 4, 5, 6, 7, 9, 11, 12, 22};
            for (const auto code : codes) {
                CAPTURE(code);
                const auto white = apply(toSrgb(code), Rgb{1.0F, 1.0F, 1.0F});
                CHECK(white[0] == doctest::Approx(1.0F).epsilon(1.0e-5));
                CHECK(white[1] == doctest::Approx(1.0F).epsilon(1.0e-5));
                CHECK(white[2] == doctest::Approx(1.0F).epsilon(1.0e-5));
            }
        }

        TEST_CASE("identity unspecified and unknown primaries follow the viewer fallback")
        {
            constexpr Matrix3 identity{{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}}};
            checkMatrix(toSrgb(1), identity);
            checkMatrix(toSrgb(2), identity);
            checkMatrix(toSrgb(99), identity);

            CHECK(isIdentity(1));
            CHECK(isIdentity(2));
            CHECK_FALSE(isIdentity(9));
            CHECK_FALSE(isIdentity(99));
        }

        TEST_CASE("explicit chromaticities build the same matrices as the coded H.273 sets at run time")
        {
            // The EXR plugin hands over a file's `chromaticities` attribute when it matches no
            // H.273 code (ACES AP0/AP1, custom sets); the run-time path must agree with the
            // compile-time tables for the sets both know.
            constexpr Chromaticities bt709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
            constexpr Chromaticities bt2020{{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
            constexpr Chromaticities p3D65{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
            constexpr Matrix3 identity{{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}}};
            checkMatrix(toSrgb(bt709), identity);
            checkMatrix(toSrgb(bt2020), toSrgb(9));
            checkMatrix(toSrgb(p3D65), toSrgb(12));

            // ACES AP0 (ACES2065-1, D60 white) and AP1 (ACEScg) to BT.709 with Bradford adaptation:
            // TB-2014-004 / colour-science "ACES2065-1 to sRGB" and "ACEScg to sRGB" matrices.
            constexpr Chromaticities ap0{{0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}};
            constexpr Matrix3 ap0Reference{{{{2.521699F, -1.134133F, -0.387566F},
                                             {-0.276478F, 1.372723F, -0.096245F},
                                             {-0.015378F, -0.152983F, 1.168361F}}}};
            checkMatrix(toSrgb(ap0), ap0Reference);
            constexpr Chromaticities ap1{{0.713F, 0.293F}, {0.165F, 0.830F}, {0.128F, 0.044F}, {0.32168F, 0.33767F}};
            constexpr Matrix3 ap1Reference{{{{1.705051F, -0.621792F, -0.083259F},
                                             {-0.130257F, 1.140805F, -0.010548F},
                                             {-0.024004F, -0.128969F, 1.152972F}}}};
            checkMatrix(toSrgb(ap1), ap1Reference);

            // A white that is not D65 is adapted even when it shares one coordinate with D65: the
            // same primaries with the white moved along y give a matrix that is not the identity.
            constexpr Chromaticities offWhite{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.3000F}};
            const auto offWhiteMatrix = toSrgb(offWhite);
            CHECK(offWhiteMatrix.rows[0][0] != doctest::Approx(1.0F).epsilon(1.0e-3));
            constexpr Chromaticities offWhiteX{
                {0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3000F, 0.3290F}};
            CHECK(toSrgb(offWhiteX).rows[0][0] != doctest::Approx(1.0F).epsilon(1.0e-3));

            // The adapted white of every set lands on sRGB white.
            for (const auto &set : {bt709, bt2020, p3D65, ap0, ap1}) {
                const auto white = apply(toSrgb(set), Rgb{1.0F, 1.0F, 1.0F});
                CHECK(white[0] == doctest::Approx(1.0F).epsilon(1.0e-4));
                CHECK(white[1] == doctest::Approx(1.0F).epsilon(1.0e-4));
                CHECK(white[2] == doctest::Approx(1.0F).epsilon(1.0e-4));
            }

            // Luminance coefficients: the middle row of RGB-to-XYZ, published for AP0 as
            // (0.3439664, 0.7281661, -0.0721325) and for BT.709 as (0.2126, 0.7152, 0.0722).
            const auto ap0Luminance = luminanceCoefficients(ap0);
            CHECK(ap0Luminance[0] == doctest::Approx(0.3439664F).epsilon(1.0e-4));
            CHECK(ap0Luminance[1] == doctest::Approx(0.7281661F).epsilon(1.0e-4));
            CHECK(ap0Luminance[2] == doctest::Approx(-0.0721325F).epsilon(1.0e-3));
            const auto bt709Luminance = luminanceCoefficients(bt709);
            CHECK(bt709Luminance[0] == doctest::Approx(luminanceCoefficients(1)[0]).epsilon(1.0e-5));
            CHECK(bt709Luminance[1] == doctest::Approx(luminanceCoefficients(1)[1]).epsilon(1.0e-5));
            CHECK(bt709Luminance[2] == doctest::Approx(luminanceCoefficients(1)[2]).epsilon(1.0e-5));
        }

        TEST_CASE("the CIE XYZ encoding converts without adaptation and without dividing by a primary's y")
        {
            // OpenEXR's XYZ convention: primaries at the XYZ axes (red (1, 0), green (0, 1), blue
            // (0, 0)) and the equal-energy white. Two of the primaries have y = 0, so the
            // derivation must not divide by it (Imf::RGBtoXYZ does not), and the white E declares
            // no viewing illuminant: the values are absolute tristimulus and go straight through
            // XYZ -> BT.709, so a D65-white picture stays D65 white (exrdisplay shows the corpus
            // pair Rec709_YC / XYZ_YC identically).
            constexpr Chromaticities xyz{{1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F / 3.0F, 1.0F / 3.0F}};
            // IEC 61966-2-1 XYZ (D65) to linear sRGB.
            constexpr Matrix3 xyzToSrgb{{{{3.240970F, -1.537383F, -0.498611F},
                                          {-0.969244F, 1.875968F, 0.041555F},
                                          {0.055630F, -0.203977F, 1.056972F}}}};
            checkMatrix(toSrgb(xyz), xyzToSrgb);
            const auto d65 = apply(toSrgb(xyz), Rgb{0.9505F, 1.0F, 1.0891F});
            CHECK(d65[0] == doctest::Approx(1.0F).epsilon(1.0e-3));
            CHECK(d65[1] == doctest::Approx(1.0F).epsilon(1.0e-3));
            CHECK(d65[2] == doctest::Approx(1.0F).epsilon(1.0e-3));
            const auto weights = luminanceCoefficients(xyz);
            CHECK(weights[0] == doctest::Approx(0.0F).epsilon(1.0e-6));
            CHECK(weights[1] == doctest::Approx(1.0F).epsilon(1.0e-6));
            CHECK(weights[2] == doctest::Approx(0.0F).epsilon(1.0e-6));

            // The rule is the white, not the primaries: CIE RGB (1931) has the same equal-energy
            // white, so its (1, 1, 1) - illuminant E - is presented as E looks next to D65, warmer.
            constexpr Chromaticities cieRgb{
                {0.7347F, 0.2653F}, {0.2738F, 0.7174F}, {0.1666F, 0.0089F}, {1.0F / 3.0F, 1.0F / 3.0F}};
            const auto e = apply(toSrgb(cieRgb), Rgb{1.0F, 1.0F, 1.0F});
            CHECK(e[0] > e[2]);
            CHECK(e[0] > 1.0F);
            // A white on E's x but not its y is not E: adapted, so its (1, 1, 1) lands on sRGB white.
            constexpr Chromaticities nearE{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {1.0F / 3.0F, 0.300F}};
            const auto adapted = apply(toSrgb(nearE), Rgb{1.0F, 1.0F, 1.0F});
            CHECK(adapted[0] == doctest::Approx(1.0F).epsilon(1.0e-4));
            CHECK(adapted[2] == doctest::Approx(1.0F).epsilon(1.0e-4));
            // A D60 white (ACES) is still adapted: its (1, 1, 1) lands on sRGB white.
            constexpr Chromaticities ap0{{0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}};
            const auto d60 = apply(toSrgb(ap0), Rgb{1.0F, 1.0F, 1.0F});
            CHECK(d60[0] == doctest::Approx(1.0F).epsilon(1.0e-4));
            CHECK(d60[2] == doctest::Approx(1.0F).epsilon(1.0e-4));
        }

        TEST_CASE("a chromaticity set is usable when its derivation is finite")
        {
            constexpr Chromaticities bt709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
            constexpr Chromaticities xyz{{1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F / 3.0F, 1.0F / 3.0F}};
            constexpr Chromaticities ap0{{0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}};
            CHECK(isUsable(bt709));
            CHECK(isUsable(xyz));
            CHECK(isUsable(ap0));

            // The two conditions Imf::RGBtoXYZ throws on: a white with y = 0 (nothing to scale
            // to) and collinear primaries (a singular matrix), plus what no library can use.
            auto whiteY0 = bt709;
            whiteY0.white.y = 0.0F;
            CHECK_FALSE(isUsable(whiteY0));
            auto whiteNegative = bt709;
            whiteNegative.white.y = -0.329F;
            CHECK_FALSE(isUsable(whiteNegative));
            constexpr Chromaticities collinear{{0.3F, 0.3F}, {0.3F, 0.3F}, {0.3F, 0.3F}, {0.3127F, 0.3290F}};
            CHECK_FALSE(isUsable(collinear));
            constexpr Chromaticities sameY{{0.1F, 0.3F}, {0.5F, 0.3F}, {0.9F, 0.3F}, {0.3127F, 0.3290F}};
            CHECK_FALSE(isUsable(sameY));
            constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
            constexpr float kInf = std::numeric_limits<float>::infinity();
            auto red = bt709;
            red.red.y = kNaN;
            CHECK_FALSE(isUsable(red));
            auto green = bt709;
            green.green.x = kNaN;
            CHECK_FALSE(isUsable(green));
            auto blue = bt709;
            blue.blue.y = kInf;
            CHECK_FALSE(isUsable(blue));
            auto white = bt709;
            white.white.x = -kInf;
            CHECK_FALSE(isUsable(white));
            // Coordinates that overflow the determinant, and a white whose XYZ overflows.
            constexpr Chromaticities hugeDeterminant{
                {3.0e38F, 0.33F}, {0.3F, 1.0F}, {0.15F, -1.0F}, {0.3127F, 0.3290F}};
            CHECK_FALSE(isUsable(hugeDeterminant));
            constexpr Chromaticities overflowingWhite{
                {0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {3.0e38F, 1.0e-5F}};
            CHECK_FALSE(isUsable(overflowingWhite));
        }

        TEST_CASE("the chromaticities of every coded primary set are exposed and unknown codes are not")
        {
            constexpr std::array<std::uint16_t, 9> codes{1, 4, 5, 6, 7, 9, 11, 12, 22};
            for (const auto code : codes) {
                CAPTURE(code);
                const auto set = chromaticities(code);
                REQUIRE(set.has_value());
                checkMatrix(toSrgb(*set), toSrgb(code));
            }
            const auto bt709 = chromaticities(1);
            REQUIRE(bt709.has_value());
            CHECK(bt709->red.x == 0.640F);
            CHECK(bt709->green.y == 0.600F);
            CHECK(bt709->blue.x == 0.150F);
            CHECK(bt709->white.y == 0.3290F);
            CHECK_FALSE(chromaticities(2).has_value());
            CHECK_FALSE(chromaticities(99).has_value());
        }

        TEST_CASE("primary classification and luminance coefficients cover every H.273 mapping")
        {
            constexpr std::array<std::uint16_t, 10> supported{1, 2, 4, 5, 6, 7, 9, 11, 12, 22};
            for (const auto code : supported) {
                CAPTURE(code);
                CHECK(isKnown(code));
                const auto coefficients = luminanceCoefficients(code);
                CHECK(coefficients[0] + coefficients[1] + coefficients[2] == doctest::Approx(1.0F).epsilon(1.0e-5));
            }
            CHECK_FALSE(isKnown(99));
            CHECK(luminanceCoefficients(99) == luminanceCoefficients(1));

            const auto bt2020 = luminanceCoefficients(9);
            CHECK(bt2020[0] == doctest::Approx(0.2627F).epsilon(1.0e-4));
            CHECK(bt2020[1] == doctest::Approx(0.6780F).epsilon(1.0e-4));
            CHECK(bt2020[2] == doctest::Approx(0.0593F).epsilon(1.0e-4));
        }

    } // namespace
} // namespace pvdkit::core::colour::Primaries
