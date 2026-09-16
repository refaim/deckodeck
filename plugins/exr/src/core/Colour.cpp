#include "core/Colour.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace pvdkit::exr
{
    namespace
    {

        using core::colour::Primaries::Chromaticities;
        using core::colour::Primaries::Chromaticity;

        bool near(const Chromaticity &a, const Chromaticity &b) noexcept
        {
            return std::abs(a.x - b.x) <= kChromaticityTolerance && std::abs(a.y - b.y) <= kChromaticityTolerance;
        }

        bool near(const Chromaticities &a, const Chromaticities &b) noexcept
        {
            return near(a.red, b.red) && near(a.green, b.green) && near(a.blue, b.blue) && near(a.white, b.white);
        }

        struct KnownSet
        {
            PrimariesMatch match = PrimariesMatch::Custom;
            std::uint16_t code = 2;
            std::string_view name;
            std::string_view interopID;
            Chromaticities chromaticities{};
        };

        // The H.273 sets are spelled here as in the shared colour module's table (H.273 Table 2,
        // the same numbers to the digit); the static_asserts below hold the two together.
        constexpr Chromaticities kRec709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
        constexpr Chromaticities kRec2020{{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
        constexpr Chromaticities kP3D65{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};

        constexpr std::array<KnownSet, 6> kKnownSets{
            KnownSet{PrimariesMatch::Rec709, 1, "Rec.709", "lin_rec709", kRec709},
            KnownSet{PrimariesMatch::Rec2020, 9, "Rec.2020", "lin_rec2020", kRec2020},
            KnownSet{PrimariesMatch::P3D65, 12, "P3-D65", "lin_p3d65", kP3D65},
            KnownSet{PrimariesMatch::AcesAp0, 2, "ACES AP0", "lin_ap0", kAcesAp0},
            KnownSet{PrimariesMatch::AcesAp1, 2, "ACES AP1", "lin_ap1", kAcesAp1},
            KnownSet{PrimariesMatch::CieXyz, 2, "CIE XYZ", "", kCieXyz},
        };

        using Flat = std::array<float, 8>;

        Flat flat(const Chromaticities &c) noexcept
        {
            return {c.red.x, c.red.y, c.green.x, c.green.y, c.blue.x, c.blue.y, c.white.x, c.white.y};
        }

        Flat coded(const std::uint16_t code) noexcept
        {
            return flat(core::colour::Primaries::chromaticities(code).value_or(Chromaticities{}));
        }

    } // namespace

    PrimariesMatch classify(const Chromaticities &chromaticities) noexcept
    {
        for (const auto &known : kKnownSets) {
            if (near(chromaticities, known.chromaticities)) {
                return known.match;
            }
        }
        return PrimariesMatch::Custom;
    }

    std::uint16_t cicpPrimaries(const PrimariesMatch match) noexcept
    {
        for (const auto &known : kKnownSets) {
            if (known.match == match) {
                return known.code;
            }
        }
        return 2;
    }

    std::string_view primariesName(const PrimariesMatch match) noexcept
    {
        for (const auto &known : kKnownSets) {
            if (known.match == match) {
                return known.name;
            }
        }
        return "custom";
    }

    std::optional<Chromaticities> interopChromaticities(const std::string_view colorInteropID) noexcept
    {
        for (const auto &known : kKnownSets) {
            if (!known.interopID.empty() && known.interopID == colorInteropID) {
                return known.chromaticities;
            }
        }
        return std::nullopt;
    }

    bool codedSetsAgreeWithTheSharedTable() noexcept
    {
        const int differing = static_cast<int>(flat(kRec709) != coded(1)) +
                              static_cast<int>(flat(kRec2020) != coded(9)) +
                              static_cast<int>(flat(kP3D65) != coded(12));
        return differing == 0;
    }

    ColourSignal resolveColour(const std::optional<Chromaticities> &attribute,
                               const std::string_view colorInteropID) noexcept
    {
        const auto set = attribute ? attribute : interopChromaticities(colorInteropID);
        if (!set) {
            return ColourSignal{};
        }
        if (!core::colour::Primaries::isUsable(*set)) {
            return ColourSignal{PrimariesMatch::Rec709, 1, std::nullopt, set};
        }
        const auto match = classify(*set);
        const auto code = cicpPrimaries(match);
        return ColourSignal{match, code, code == 2 ? set : std::nullopt, std::nullopt};
    }

} // namespace pvdkit::exr
