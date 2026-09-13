#include <array>
#include <cmath>
#include <cstdint>

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
