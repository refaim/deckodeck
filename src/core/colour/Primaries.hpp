#pragma once

#include <array>
#include <cstddef>
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
    /// `matrix` times `colour`, each row accumulated left to right from zero. Inline: it runs per
    /// pixel in the presentation, and the tests' scalar reference must evaluate the very same
    /// expression (an out-of-line copy in another translation unit could be compiled differently).
    [[nodiscard]] constexpr Rgb apply(const Matrix3 &matrix, const Rgb &colour) noexcept
    {
        Rgb result{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                result[row] += matrix.rows[row][column] * colour[column];
            }
        }
        return result;
    }
    [[nodiscard]] Rgb luminanceCoefficients(std::uint16_t primaries) noexcept;
    /// The Y row of the run-time RGB to XYZ matrix. Requires `isUsable(chromaticities)`.
    [[nodiscard]] Rgb luminanceCoefficients(const Chromaticities &chromaticities) noexcept;
    /// True when the run-time derivation is finite: every coordinate finite, the white's y
    /// positive, the primaries not collinear, and the matrices and coefficients above finite. A
    /// container checks its attribute with this before handing it over (`ImageMeta::chromaticities`);
    /// the presentation ignores a set that fails it and uses the coded primaries.
    [[nodiscard]] bool isUsable(const Chromaticities &chromaticities) noexcept;
    /// The H.273 Table 2 chromaticities of a coded set; nullopt for code 2 and unknown codes.
    /// constexpr so a plugin can spell its own table with these values (plugins/exr/src/core/
    /// Colour.cpp) instead of a copy it has to keep in step.
    [[nodiscard]] constexpr std::optional<Chromaticities> chromaticities(std::uint16_t primaries) noexcept;
    [[nodiscard]] bool isKnown(std::uint16_t primaries) noexcept;
    [[nodiscard]] bool isIdentity(std::uint16_t primaries) noexcept;

    namespace detail
    {
        inline constexpr Chromaticity kD65{0.3127F, 0.3290F};
        inline constexpr Chromaticity kIlluminantC{0.3100F, 0.3160F};
        inline constexpr Chromaticity kDciWhite{0.3140F, 0.3510F};

        // H.273 Table 2 chromaticities. These were cross-checked against zimg's
        // colorspace_param.h; the matrices in Primaries.cpp are derived from them, never pasted.
        inline constexpr Chromaticities kBt709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, kD65};
        inline constexpr Chromaticities kBt470M{{0.670F, 0.330F}, {0.210F, 0.710F}, {0.140F, 0.080F}, kIlluminantC};
        inline constexpr Chromaticities kBt470Bg{{0.640F, 0.330F}, {0.290F, 0.600F}, {0.150F, 0.060F}, kD65};
        inline constexpr Chromaticities kSmpteC{{0.630F, 0.340F}, {0.310F, 0.595F}, {0.155F, 0.070F}, kD65};
        inline constexpr Chromaticities kBt2020{{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, kD65};
        inline constexpr Chromaticities kP3Dci{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, kDciWhite};
        inline constexpr Chromaticities kP3D65{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, kD65};
        inline constexpr Chromaticities kEbu3213{{0.630F, 0.340F}, {0.295F, 0.605F}, {0.155F, 0.077F}, kD65};
    } // namespace detail

    constexpr std::optional<Chromaticities> chromaticities(const std::uint16_t primaries) noexcept
    {
        switch (primaries) {
        case 1:
            return detail::kBt709;
        case 4:
            return detail::kBt470M;
        case 5:
            return detail::kBt470Bg;
        case 6:
        case 7:
            return detail::kSmpteC;
        case 9:
            return detail::kBt2020;
        case 11:
            return detail::kP3Dci;
        case 12:
            return detail::kP3D65;
        case 22:
            return detail::kEbu3213;
        default:
            return std::nullopt;
        }
    }

} // namespace pvdkit::core::colour::Primaries
