#pragma once

#include <array>
#include <cstdint>

namespace pvdkit::core::colour::Primaries
{

    using Rgb = std::array<float, 3>;

    struct Matrix3
    {
        std::array<std::array<float, 3>, 3> rows;
    };

    /// Returns the linear-RGB matrix from an H.273 primary set to BT.709/sRGB. Unspecified code 2
    /// is identity; an unknown code also returns identity so a viewer can still present the image.
    [[nodiscard]] Matrix3 toSrgb(std::uint16_t primaries) noexcept;
    [[nodiscard]] Rgb apply(const Matrix3 &matrix, const Rgb &colour) noexcept;
    [[nodiscard]] Rgb luminanceCoefficients(std::uint16_t primaries) noexcept;
    [[nodiscard]] bool isKnown(std::uint16_t primaries) noexcept;
    [[nodiscard]] bool isIdentity(std::uint16_t primaries) noexcept;

} // namespace pvdkit::core::colour::Primaries
