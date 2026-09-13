#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/colour/ToneMap.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour::ToneMap
{
    namespace
    {

        TEST_CASE("BT.2390 uses an identity segment before its Hermite knee")
        {
            const Eetf curve{1'000.0F};
            CHECK(curve.kneeNits() == doctest::Approx(27.8585F).epsilon(2.0e-4));
            CHECK(curve.compressionCurve(0.0F) == 0.0F);
            CHECK(curve.compressionCurve(0.25F) == 0.25F);
            CHECK(curve.compressionCurve(curve.knee()) == doctest::Approx(curve.knee()).epsilon(1.0e-6));
            CHECK(curve.compressionCurve(1.0F) < 1.0F);
            CHECK(curve.compressionCurve(-1.0F) == 0.0F);
            CHECK(curve.compressionCurve(2.0F) == curve.compressionCurve(1.0F));
        }

        TEST_CASE("BT.2390 normalizes PQ over the complete source black-to-white span")
        {
            const Eetf curve{1'000.0F};
            const auto pqBlack = Transfer::nitsToPq(0.0F);
            const auto sourceSpan = Transfer::nitsToPq(1'000.0F) - pqBlack;
            const auto normalizedTargetWhite = (Transfer::nitsToPq(kTargetPeakNits) - pqBlack) / sourceSpan;
            const auto expectedKnee = 1.5F * normalizedTargetWhite - 0.5F;

            CHECK(std::abs(pqBlack - 7.3095590e-7F) <= 1.0e-12F);
            CHECK(std::abs(normalizedTargetWhite - 0.67579127F) <= 5.0e-6F);
            CHECK(std::abs(curve.knee() - expectedKnee) <= 1.0e-7F);
            CHECK(std::abs(curve.knee() - 0.51368690F) <= 5.0e-6F);
        }

        TEST_CASE("BT.2390 1000-to-100-nit values follow the Annex 5 worked curve")
        {
            const Eetf curve{1'000.0F};
            CHECK(curve.mapNits(0.0F) == doctest::Approx(0.00005F).epsilon(3.0e-4));
            CHECK(curve.mapNits(10.0F) == doctest::Approx(0.10252768F).epsilon(3.0e-4));
            CHECK(curve.mapNits(100.0F) == doctest::Approx(0.69660405F).epsilon(3.0e-4));
            CHECK(curve.mapNits(400.0F) == doctest::Approx(0.97767375F).epsilon(3.0e-4));
            CHECK(curve.mapNits(1'000.0F) == doctest::Approx(1.0F).epsilon(1.0e-6));
        }

        TEST_CASE("BT.2390 is monotone and bounded over the source range")
        {
            const Eetf curve{1'000.0F};
            auto previous = curve.mapNits(0.0F);
            for (std::uint32_t sample = 1; sample <= 10'000; ++sample) {
                const auto mapped = curve.mapNits(static_cast<float>(sample) / 10.0F);
                CHECK(mapped >= previous);
                CHECK(mapped <= 1.0F);
                previous = mapped;
            }
            CHECK(curve.mapNits(-1.0F) == curve.mapNits(0.0F));
            CHECK(curve.mapNits(2'000.0F) == 1.0F);
        }

        TEST_CASE("a target equal to the source leaves the compression curve degenerate")
        {
            const Eetf curve{100.0F};
            CHECK(curve.knee() >= 1.0F);
            CHECK(curve.compressionCurve(0.0F) == 0.0F);
            CHECK(curve.compressionCurve(0.5F) == 0.5F);
            CHECK(curve.compressionCurve(1.0F) == 1.0F);
            CHECK(curve.mapNits(100.0F) == doctest::Approx(1.0F));
        }

        TEST_CASE("maxRGB tone mapping preserves channel ratios and defines exact black")
        {
            const Eetf curve{1'000.0F};
            const auto mapped = applyMaxRgb(curve, Rgb{500.0F, 250.0F, 125.0F});
            CHECK(mapped[0] == doctest::Approx(curve.mapNits(500.0F)).epsilon(1.0e-6));
            CHECK(mapped[1] == doctest::Approx(mapped[0] * 0.5F).epsilon(1.0e-6));
            CHECK(mapped[2] == doctest::Approx(mapped[0] * 0.25F).epsilon(1.0e-6));

            constexpr auto black = kTargetBlackNits / kTargetPeakNits;
            CHECK(applyMaxRgb(curve, Rgb{0.0F, 0.0F, 0.0F}) == Rgb{black, black, black});
            CHECK(applyMaxRgb(curve, Rgb{-1.0F, -2.0F, -3.0F}) == Rgb{black, black, black});
        }

    } // namespace
} // namespace pvdkit::core::colour::ToneMap
