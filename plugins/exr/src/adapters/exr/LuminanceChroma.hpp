#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Imath/ImathVec.h>
#include <OpenEXR/ImfRgba.h>

#include "core/colour/Primaries.hpp"

namespace pvdkit::exr
{

    /// The library's chroma reconstruction filter spans 27 samples; a row is padded by 13 on each side.
    inline constexpr std::size_t kChromaTaps = 27;
    inline constexpr std::size_t kChromaPad = kChromaTaps / 2;

    /// The luminance weights `Imf::RgbaInputFile` reconstructs a file with: the Y row of the
    /// library's RGB to XYZ over the file's chromaticities (Rec.709 when the attribute is absent,
    /// its own default), normalised as `RgbaYca::computeYw` normalises. The library's arithmetic
    /// (ImfChromaticities.cpp, RGBtoXYZ at Y = 1) is spelled out here, expression for expression,
    /// so the weights are its bits and the reconstruction rounds the halves the library rounds -
    /// without calling it: `computeYw` throws on a white with y = 0 and on collinear primaries,
    /// and no adapter may catch. The set is one `Primaries::isUsable` accepted (Colour.cpp), so
    /// the two divisions are by a positive white y and a non-zero determinant.
    [[nodiscard]] Imath::V3f luminanceWeights(
        const std::optional<core::colour::Primaries::Chromaticities> &chromaticities) noexcept;

    /// A luminance/chroma part as half `Rgba` rows (Y in g, RY/BY in r/b at even positions, A in a),
    /// each row padded by `kChromaPad` pixels on both sides for the horizontal filter, and the
    /// reconstruction to RGBA the way `Imf::RgbaInputFile` performs it: horizontal chroma
    /// reconstruction on even rows, vertical on odd rows over 27 neighbours (edge rows repeated as
    /// the library repeats them), YCA to RGBA, then the saturation fix over each row's neighbours.
    /// The four RgbaYca functions used (reconstructChromaHoriz, reconstructChromaVert, YCAtoRGBA,
    /// fixSaturation) are arithmetic over the rows and throw nothing. `height` is at least 2 and
    /// even: the Core refuses a data window whose height is not a multiple of the chroma
    /// channels' 2x y sampling (validation.c, validate_channels), and the edge handling below
    /// (the row after the last is the second-to-last) relies on that.
    class LuminanceChromaImage
    {
      public:
        LuminanceChromaImage(std::uint32_t width, std::uint32_t height);

        [[nodiscard]] std::uint32_t width() const noexcept;
        /// The padded row `y`; pixel `x` of the image is at index `x + kChromaPad`.
        [[nodiscard]] std::span<Imf::Rgba> row(std::uint32_t y) noexcept;
        [[nodiscard]] std::size_t paddedWidth() const noexcept;

        /// Interleaved float RGBA, `width * height * 4` values, row-major, reconstructed with
        /// `luminanceWeights`.
        [[nodiscard]] std::vector<float> reconstruct(const Imath::V3f &luminanceWeights) const;

      private:
        std::uint32_t width_;
        std::uint32_t height_;
        std::vector<Imf::Rgba> rows_;
    };

} // namespace pvdkit::exr
