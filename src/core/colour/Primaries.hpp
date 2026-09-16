#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace pvdkit::core::colour::Primaries
{

    using Rgb = std::array<float, 3>;

    struct Matrix3
    {
        std::array<std::array<float, 3>, 3> rows;
    };

    /// One CIE xy chromaticity coordinate.
    struct Chromaticity
    {
        float x;
        float y;
    };

    /// The xy chromaticities of an RGB colour space: three primaries and the white point. This is
    /// what a container hands over when its primaries are not an H.273 code (OpenEXR's
    /// `chromaticities` attribute, ACES AP0/AP1); `ImageMeta::chromaticities` carries it to the
    /// presentation, which derives the conversion matrix at run time from these values.
    struct Chromaticities
    {
        Chromaticity red;
        Chromaticity green;
        Chromaticity blue;
        Chromaticity white;
    };

    /// Returns the linear-RGB matrix from an H.273 primary set to BT.709/sRGB. Unspecified code 2
    /// is identity; an unknown code also returns identity so a viewer can still present the image.
    [[nodiscard]] Matrix3 toSrgb(std::uint16_t primaries) noexcept;
    /// The same matrix derived at run time from explicit chromaticities: RGB to XYZ in the column
    /// form (each primary's (x, y, 1 - x - y) scaled so that RGB (1, 1, 1) lands on the white at
    /// Y = 1, as Imf::RGBtoXYZ derives it; nothing divides by a primary's y, so the CIE XYZ
    /// encoding with two primaries at y = 0 is a valid set), Bradford adaptation of the white to
    /// D65, XYZ to BT.709. The white is not adapted when it is the equal-energy white E (within
    /// 1e-3): CIE XYZ and the other colorimetric encodings declare no viewing illuminant with it,
    /// their values are absolute tristimulus and pass through unadapted (a D65-white picture stored
    /// as XYZ stays D65 white). Requires `isUsable(chromaticities)`.
    [[nodiscard]] Matrix3 toSrgb(const Chromaticities &chromaticities) noexcept;
    [[nodiscard]] Rgb apply(const Matrix3 &matrix, const Rgb &colour) noexcept;
    [[nodiscard]] Rgb luminanceCoefficients(std::uint16_t primaries) noexcept;
    /// The Y row of the run-time RGB to XYZ matrix. Requires `isUsable(chromaticities)`.
    [[nodiscard]] Rgb luminanceCoefficients(const Chromaticities &chromaticities) noexcept;
    /// True when the run-time derivation is finite: every coordinate finite, the white's y
    /// positive, the primaries not collinear, and the matrices and coefficients above finite. A
    /// container checks its attribute with this before handing it over (`ImageMeta::chromaticities`);
    /// the presentation ignores a set that fails it and uses the coded primaries.
    [[nodiscard]] bool isUsable(const Chromaticities &chromaticities) noexcept;
    /// The H.273 Table 2 chromaticities of a coded set; nullopt for code 2 and unknown codes.
    [[nodiscard]] std::optional<Chromaticities> chromaticities(std::uint16_t primaries) noexcept;
    [[nodiscard]] bool isKnown(std::uint16_t primaries) noexcept;
    [[nodiscard]] bool isIdentity(std::uint16_t primaries) noexcept;

} // namespace pvdkit::core::colour::Primaries
