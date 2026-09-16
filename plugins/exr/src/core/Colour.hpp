#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/colour/Primaries.hpp"

namespace pvdkit::exr
{

    /// The primary sets the plugin recognises in a `chromaticities` attribute.
    enum class PrimariesMatch : std::uint8_t
    {
        Rec709,
        Rec2020,
        P3D65,
        AcesAp0,
        AcesAp1,
        CieXyz,
        Custom
    };

    /// ACES AP0 (ACES2065-1) and AP1 (ACEScg) primaries with the ACES D60 white (SMPTE ST 2065-1).
    inline constexpr core::colour::Primaries::Chromaticities kAcesAp0{
        {0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}};
    inline constexpr core::colour::Primaries::Chromaticities kAcesAp1{
        {0.713F, 0.293F}, {0.165F, 0.830F}, {0.128F, 0.044F}, {0.32168F, 0.33767F}};
    /// CIE XYZ as OpenEXR spells it (the corpus file XYZ_YC.exr): the primaries at the XYZ axes,
    /// the equal-energy white. The values are absolute tristimulus, presented without adaptation.
    inline constexpr core::colour::Primaries::Chromaticities kCieXyz{
        {1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F / 3.0F, 1.0F / 3.0F}};

    /// Every coordinate within this distance of a known set matches it.
    inline constexpr float kChromaticityTolerance = 1.0e-3F;

    [[nodiscard]] PrimariesMatch classify(const core::colour::Primaries::Chromaticities &chromaticities) noexcept;
    /// The H.273 code of a matched set: 1, 9 or 12; 2 (unspecified) for the ACES sets and custom ones.
    [[nodiscard]] std::uint16_t cicpPrimaries(PrimariesMatch match) noexcept;
    [[nodiscard]] std::string_view primariesName(PrimariesMatch match) noexcept;
    /// The chromaticities named by a linear OpenEXR 3.4 `colorInteropID` (`lin_rec709`,
    /// `lin_rec2020`, `lin_p3d65`, `lin_ap0`, `lin_ap1`); nullopt for any other id.
    [[nodiscard]] std::optional<core::colour::Primaries::Chromaticities> interopChromaticities(
        std::string_view colorInteropID) noexcept;
    /// What the decoder hands the shared presentation: the CICP primaries code and, for sets the
    /// code cannot express, the explicit chromaticities.
    struct ColourSignal
    {
        PrimariesMatch match = PrimariesMatch::Rec709;
        std::uint16_t primaries = 1;
        std::optional<core::colour::Primaries::Chromaticities> chromaticities;
        /// The attribute's values when no derivation can use them (`Primaries::isUsable`): Rec.709
        /// stands in above, and the info line prints these so the file's fault is visible.
        std::optional<core::colour::Primaries::Chromaticities> unusable;
    };

    /// The `chromaticities` attribute wins; a known linear `colorInteropID` is the fallback; neither
    /// means Rec.709 (the OpenEXR default). A set that fails `Primaries::isUsable` (a white with
    /// y = 0, collinear primaries, non-finite values - the sets the library's own RGBtoXYZ refuses)
    /// is not handed to any matrix: the signal is Rec.709 with the set in `unusable`.
    [[nodiscard]] ColourSignal resolveColour(const std::optional<core::colour::Primaries::Chromaticities> &attribute,
                                             std::string_view colorInteropID) noexcept;

} // namespace pvdkit::exr
