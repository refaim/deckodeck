#include <array>
#include <cmath>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour::Transfer
{
    namespace
    {

        TEST_CASE("published transfer-function anchors decode to linear light")
        {
            CHECK(srgbToLinear(0.5F) == doctest::Approx(0.21404114F).epsilon(1.0e-6));
            CHECK(bt1886ToLinear(0.5F) == doctest::Approx(0.18946457F).epsilon(1.0e-6));
            CHECK(gamma22ToLinear(0.5F) == doctest::Approx(0.21763764F).epsilon(1.0e-6));
            CHECK(gamma28ToLinear(0.5F) == doctest::Approx(0.14358729F).epsilon(1.0e-6));
            CHECK(linearToLinear(0.5F) == 0.5F);

            // ITU-R BT.2100-3 Table 4: PQ code 0.5080784215 represents 100 cd/m2.
            CHECK(pqToNits(0.5080784215F) == doctest::Approx(100.0F).epsilon(2.0e-5));
            // ITU-R BT.2100-3 Table 5: the HLG inverse OETF maps 0.5 to 1/12 scene light.
            CHECK(hlgToScene(0.5F) == doctest::Approx(1.0F / 12.0F).epsilon(1.0e-6));
        }

        TEST_CASE("every supported CICP transfer code selects its specified curve")
        {
            for (const auto code : std::array<std::uint16_t, 4>{1, 6, 14, 15}) {
                CAPTURE(code);
                CHECK(toLinear(code, 0.5F) == doctest::Approx(bt1886ToLinear(0.5F)));
            }
            CHECK(toLinear(13, 0.5F) == doctest::Approx(srgbToLinear(0.5F)));
            CHECK(toLinear(8, 0.5F) == 0.5F);
            CHECK(toLinear(4, 0.5F) == doctest::Approx(gamma22ToLinear(0.5F)));
            CHECK(toLinear(5, 0.5F) == doctest::Approx(gamma28ToLinear(0.5F)));
            CHECK(toLinear(16, 0.5080784215F) == doctest::Approx(100.0F).epsilon(2.0e-5));
            CHECK(toLinear(18, 0.5F) == doctest::Approx(1.0F / 12.0F).epsilon(1.0e-6));

            // H.273 code 2 and unknown values use the viewer's documented sRGB fallback.
            CHECK(toLinear(2, 0.5F) == doctest::Approx(srgbToLinear(0.5F)));
            CHECK(toLinear(99, 0.5F) == doctest::Approx(srgbToLinear(0.5F)));
        }

        TEST_CASE("every transfer curve round-trips across its full encoded domain")
        {
            constexpr std::array<std::uint16_t, 12> codes{1, 6, 14, 15, 13, 8, 4, 5, 16, 18, 2, 99};
            for (const auto code : codes) {
                CAPTURE(code);
                for (std::uint32_t sample = 0; sample < 4096; ++sample) {
                    const auto encoded = static_cast<float>(sample) / 4095.0F;
                    CHECK(fromLinear(code, toLinear(code, encoded)) == doctest::Approx(encoded).epsilon(1.0e-5));
                }
            }
        }

        TEST_CASE("transfer classification covers known HDR and fallback values")
        {
            for (const auto code : std::array<std::uint16_t, 10>{1, 2, 4, 5, 6, 8, 13, 14, 15, 16}) {
                CAPTURE(code);
                CHECK(isKnown(code));
            }
            CHECK(isKnown(18));
            CHECK_FALSE(isKnown(3));
            CHECK_FALSE(isHdr(13));
            CHECK(isHdr(16));
            CHECK(isHdr(18));
        }

        TEST_CASE("transfer functions have stable endpoint and out-of-domain behaviour")
        {
            // SMPTE ST 2084 inverse EOTF evaluates its complete equation at black: c1^m2.
            // This endpoint and the 100-nit anchor also exercise the scalar path checked against
            // libjxl's transfer_functions_test.cc random reference comparison.
            constexpr auto pqBlack = 7.3095590e-7F;
            CHECK(std::abs(nitsToPq(0.0F) - pqBlack) <= 1.0e-12F);
            CHECK(std::abs(nitsToPq(100.0F) - 0.5080784215F) <= 1.0e-6F);
            CHECK(nitsToPq(10'000.0F) == doctest::Approx(1.0F));

            CHECK(srgbToLinear(0.04045F) == doctest::Approx(0.0031308F).epsilon(2.0e-6));
            CHECK(linearToSrgb(0.0031308F) == doctest::Approx(0.04045F).epsilon(2.0e-6));
            CHECK(srgbToLinear(-1.0F) == 0.0F);
            CHECK(bt1886ToLinear(-1.0F) == 0.0F);
            CHECK(gamma22ToLinear(-1.0F) == 0.0F);
            CHECK(gamma28ToLinear(-1.0F) == 0.0F);
            CHECK(linearToLinear(-1.0F) == 0.0F);
            CHECK(pqToNits(-1.0F) == 0.0F);
            CHECK(pqToNits(2.0F) == doctest::Approx(10'000.0F));
            CHECK(nitsToPq(-1.0F) == nitsToPq(0.0F));
            CHECK(nitsToPq(20'000.0F) == doctest::Approx(1.0F));
            CHECK(hlgToScene(-1.0F) == 0.0F);
            CHECK(sceneToHlg(-1.0F) == 0.0F);
            CHECK(linearToSrgb(-1.0F) < 0.0F);
        }

    } // namespace
} // namespace pvdkit::core::colour::Transfer
