#include <optional>
#include <ostream>
#include <string>

#include <doctest/doctest.h>

#include "core/Colour.hpp"
#include "core/colour/Primaries.hpp"

namespace pvdkit::exr
{
    namespace
    {

        using core::colour::Primaries::Chromaticities;

        constexpr Chromaticities kRec709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
        constexpr Chromaticities kRec2020{{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
        constexpr Chromaticities kP3D65{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
        constexpr Chromaticities kXyz{{1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F / 3.0F, 1.0F / 3.0F}};

        TEST_CASE("the known primary sets are matched within a tolerance of 1e-3 per coordinate")
        {
            CHECK(classify(kRec709) == PrimariesMatch::Rec709);
            CHECK(classify(kRec2020) == PrimariesMatch::Rec2020);
            CHECK(classify(kP3D65) == PrimariesMatch::P3D65);
            CHECK(classify(kAcesAp0) == PrimariesMatch::AcesAp0);
            CHECK(classify(kAcesAp1) == PrimariesMatch::AcesAp1);
            CHECK(classify(kXyz) == PrimariesMatch::CieXyz);
            constexpr Chromaticities kProPhoto{
                {0.7347F, 0.2653F}, {0.1596F, 0.8404F}, {0.0366F, 0.0001F}, {0.3457F, 0.3585F}};
            CHECK(classify(kProPhoto) == PrimariesMatch::Custom);

            // Values written by another library with float rounding still match ...
            Chromaticities nearly = kAcesAp0;
            nearly.red.x += 0.0009F;
            nearly.white.y -= 0.0009F;
            CHECK(classify(nearly) == PrimariesMatch::AcesAp0);
            // ... while a coordinate off by more than the tolerance does not.
            Chromaticities off = kRec709;
            off.green.y += 0.0011F;
            CHECK(classify(off) == PrimariesMatch::Custom);
            Chromaticities offWhite = kRec709;
            offWhite.white.x = 0.3457F;
            CHECK(classify(offWhite) == PrimariesMatch::Custom);
            Chromaticities offBlue = kRec709;
            offBlue.blue.y = 0.070F;
            CHECK(classify(offBlue) == PrimariesMatch::Custom);
            Chromaticities offRedY = kRec709;
            offRedY.red.y = 0.340F;
            CHECK(classify(offRedY) == PrimariesMatch::Custom);
        }

        TEST_CASE("every match maps to its H.273 code or to unspecified and carries a name")
        {
            CHECK(cicpPrimaries(PrimariesMatch::Rec709) == 1);
            CHECK(cicpPrimaries(PrimariesMatch::Rec2020) == 9);
            CHECK(cicpPrimaries(PrimariesMatch::P3D65) == 12);
            CHECK(cicpPrimaries(PrimariesMatch::AcesAp0) == 2);
            CHECK(cicpPrimaries(PrimariesMatch::AcesAp1) == 2);
            CHECK(cicpPrimaries(PrimariesMatch::CieXyz) == 2);
            CHECK(cicpPrimaries(PrimariesMatch::Custom) == 2);
            CHECK(primariesName(PrimariesMatch::Rec709) == "Rec.709");
            CHECK(primariesName(PrimariesMatch::Rec2020) == "Rec.2020");
            CHECK(primariesName(PrimariesMatch::P3D65) == "P3-D65");
            CHECK(primariesName(PrimariesMatch::AcesAp0) == "ACES AP0");
            CHECK(primariesName(PrimariesMatch::AcesAp1) == "ACES AP1");
            CHECK(primariesName(PrimariesMatch::CieXyz) == "CIE XYZ");
            CHECK(primariesName(PrimariesMatch::Custom) == "custom");
        }

        TEST_CASE("the linear colorInteropID values name the known sets and anything else is ignored")
        {
            const auto rec709 = interopChromaticities("lin_rec709");
            REQUIRE(rec709.has_value());
            CHECK(classify(*rec709) == PrimariesMatch::Rec709);
            const auto rec2020 = interopChromaticities("lin_rec2020");
            REQUIRE(rec2020.has_value());
            CHECK(classify(*rec2020) == PrimariesMatch::Rec2020);
            const auto p3 = interopChromaticities("lin_p3d65");
            REQUIRE(p3.has_value());
            CHECK(classify(*p3) == PrimariesMatch::P3D65);
            const auto ap0 = interopChromaticities("lin_ap0");
            REQUIRE(ap0.has_value());
            CHECK(classify(*ap0) == PrimariesMatch::AcesAp0);
            const auto ap1 = interopChromaticities("lin_ap1");
            REQUIRE(ap1.has_value());
            CHECK(classify(*ap1) == PrimariesMatch::AcesAp1);
            CHECK_FALSE(interopChromaticities("srgb_texture").has_value());
            CHECK_FALSE(interopChromaticities("").has_value());
        }

        TEST_CASE("the colour signal combines the chromaticities attribute and the interop fallback")
        {
            // Absent both: Rec.709 by definition, coded, nothing custom.
            const auto plain = resolveColour(std::nullopt, "");
            CHECK(plain.match == PrimariesMatch::Rec709);
            CHECK(plain.primaries == 1);
            CHECK_FALSE(plain.chromaticities.has_value());

            // A coded match keeps the code and hands no custom set to the presentation.
            const auto coded = resolveColour(kRec2020, "lin_ap0");
            CHECK(coded.match == PrimariesMatch::Rec2020);
            CHECK(coded.primaries == 9);
            CHECK_FALSE(coded.chromaticities.has_value());

            // ACES and custom sets are unspecified codes plus the file's own values.
            const auto aces = resolveColour(kAcesAp1, "");
            CHECK(aces.match == PrimariesMatch::AcesAp1);
            CHECK(aces.primaries == 2);
            REQUIRE(aces.chromaticities.has_value());
            CHECK(aces.chromaticities->green.y == kAcesAp1.green.y);
            const auto xyz = resolveColour(kXyz, "");
            CHECK(xyz.match == PrimariesMatch::CieXyz);
            CHECK(xyz.primaries == 2);
            REQUIRE(xyz.chromaticities.has_value());
            CHECK(xyz.chromaticities->white.x == kXyz.white.x);
            CHECK_FALSE(xyz.unusable.has_value());
            constexpr Chromaticities kProPhoto{
                {0.7347F, 0.2653F}, {0.1596F, 0.8404F}, {0.0366F, 0.0001F}, {0.3457F, 0.3585F}};
            const auto custom = resolveColour(kProPhoto, "");
            CHECK(custom.match == PrimariesMatch::Custom);
            CHECK(custom.primaries == 2);
            REQUIRE(custom.chromaticities.has_value());
            CHECK(custom.chromaticities->white.x == kProPhoto.white.x);

            // The interop id is the fallback when the attribute is absent, known ids only.
            const auto interop = resolveColour(std::nullopt, "lin_ap0");
            CHECK(interop.match == PrimariesMatch::AcesAp0);
            CHECK(interop.primaries == 2);
            CHECK(interop.chromaticities.has_value());
            const auto unknownInterop = resolveColour(std::nullopt, "srgb_texture");
            CHECK(unknownInterop.match == PrimariesMatch::Rec709);
            CHECK_FALSE(unknownInterop.chromaticities.has_value());
        }

        TEST_CASE("a chromaticities attribute no derivation can use falls back to Rec.709 and is reported")
        {
            // The two sets the library itself refuses (Imf::RGBtoXYZ throws on them): a white with
            // y = 0 and collinear primaries; neither reaches any matrix, the picture is shown as
            // Rec.709 and the attribute's values go to the info line.
            Chromaticities whiteY0 = kRec709;
            whiteY0.white.y = 0.0F;
            const auto fallback = resolveColour(whiteY0, "lin_ap0");
            CHECK(fallback.match == PrimariesMatch::Rec709);
            CHECK(fallback.primaries == 1);
            CHECK_FALSE(fallback.chromaticities.has_value());
            REQUIRE(fallback.unusable.has_value());
            CHECK(fallback.unusable->white.y == 0.0F);
            CHECK(fallback.unusable->red.x == kRec709.red.x);

            constexpr Chromaticities collinear{{0.3F, 0.3F}, {0.3F, 0.3F}, {0.3F, 0.3F}, {0.3127F, 0.3290F}};
            const auto degenerate = resolveColour(collinear, "");
            CHECK(degenerate.match == PrimariesMatch::Rec709);
            CHECK(degenerate.primaries == 1);
            CHECK_FALSE(degenerate.chromaticities.has_value());
            REQUIRE(degenerate.unusable.has_value());
            CHECK(degenerate.unusable->green.x == 0.3F);

            // A usable set that merely differs from every known one is still custom, not a fallback.
            Chromaticities odd = kRec709;
            odd.green.y = 0.55F;
            const auto custom = resolveColour(odd, "");
            CHECK(custom.match == PrimariesMatch::Custom);
            CHECK(custom.chromaticities.has_value());
            CHECK_FALSE(custom.unusable.has_value());
        }

    } // namespace
} // namespace pvdkit::exr
