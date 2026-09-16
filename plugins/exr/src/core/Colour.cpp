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

        // The H.273 sets are the shared colour module's own values (H.273 Table 2), read at
        // compile time; there is no local copy to keep in step.
        constexpr Chromaticities kRec709 = *core::colour::Primaries::chromaticities(1);
        constexpr Chromaticities kRec2020 = *core::colour::Primaries::chromaticities(9);
        constexpr Chromaticities kP3D65 = *core::colour::Primaries::chromaticities(12);

        constexpr std::array<KnownSet, 6> kKnownSets{
            KnownSet{PrimariesMatch::Rec709, 1, "Rec.709", "lin_rec709", kRec709},
            KnownSet{PrimariesMatch::Rec2020, 9, "Rec.2020", "lin_rec2020", kRec2020},
            KnownSet{PrimariesMatch::P3D65, 12, "P3-D65", "lin_p3d65", kP3D65},
            KnownSet{PrimariesMatch::AcesAp0, 2, "ACES AP0", "lin_ap0", kAcesAp0},
            KnownSet{PrimariesMatch::AcesAp1, 2, "ACES AP1", "lin_ap1", kAcesAp1},
            KnownSet{PrimariesMatch::CieXyz, 2, "CIE XYZ", "", kCieXyz},
        };

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
