#include "adapters/exr/LuminanceChroma.hpp"

#include <algorithm>
#include <array>

#include <OpenEXR/ImfRgbaYca.h>

namespace pvdkit::exr
{
    namespace
    {

        static_assert(kChromaTaps == static_cast<std::size_t>(Imf::RgbaYca::N));
        static_assert(kChromaPad == static_cast<std::size_t>(Imf::RgbaYca::N2));

        // Rows the library reads beyond the image: the first row above the top, the second-to-last
        // row below the bottom (ImfRgbaFile.cpp, FromYca::readYCAScanLine clamps y to yMax - 1).
        // `height >= 2` (LuminanceChroma.hpp: the Core validated it even), so the subtraction
        // cannot wrap.
        std::size_t clampRow(const std::int64_t row, const std::uint32_t height) noexcept
        {
            if (row < 0) {
                return 0;
            }
            if (row >= height) {
                return height - 2;
            }
            return static_cast<std::size_t>(row);
        }

    } // namespace

    Imath::V3f luminanceWeights(const std::optional<core::colour::Primaries::Chromaticities> &chromaticities) noexcept
    {
        // Imf::Chromaticities' default: Rec.709 with the D65 white.
        constexpr core::colour::Primaries::Chromaticities kDefault{
            {0.6400F, 0.3300F}, {0.3000F, 0.6000F}, {0.1500F, 0.0600F}, {0.3127F, 0.3290F}};
        const auto &c = chromaticities.value_or(kDefault);
        // ImfChromaticities.cpp, RGBtoXYZ (Y = 1): the white's X and Z, the common denominator,
        // the three scale factors' numerators; the Y row is (Sr * red.y, Sg * green.y, Sb * blue.y).
        const float y = 1.0F;
        const float x = c.white.x * y / c.white.y;
        const float z = (1 - c.white.x - c.white.y) * y / c.white.y;
        const float d =
            c.red.x * (c.blue.y - c.green.y) + c.blue.x * (c.green.y - c.red.y) + c.green.x * (c.red.y - c.blue.y);
        const float redNumerator = (x * (c.blue.y - c.green.y) - c.green.x * (y * (c.blue.y - 1) + c.blue.y * (x + z)) +
                                    c.blue.x * (y * (c.green.y - 1) + c.green.y * (x + z)));
        const float greenNumerator = (x * (c.red.y - c.blue.y) + c.red.x * (y * (c.blue.y - 1) + c.blue.y * (x + z)) -
                                      c.blue.x * (y * (c.red.y - 1) + c.red.y * (x + z)));
        const float blueNumerator = (x * (c.green.y - c.red.y) - c.red.x * (y * (c.green.y - 1) + c.green.y * (x + z)) +
                                     c.green.x * (y * (c.red.y - 1) + c.red.y * (x + z)));
        const float red = redNumerator / d;
        const float green = greenNumerator / d;
        const float blue = blueNumerator / d;
        const Imath::V3f weights{red * c.red.y, green * c.green.y, blue * c.blue.y};
        return weights / (weights.x + weights.y + weights.z);
    }

    LuminanceChromaImage::LuminanceChromaImage(const std::uint32_t width, const std::uint32_t height)
        : width_(width), height_(height),
          rows_(static_cast<std::size_t>(width + kChromaTaps - 1) * height,
                Imf::Rgba{Imath::half{0.0F}, Imath::half{0.0F}, Imath::half{0.0F}, Imath::half{1.0F}})
    {
    }

    std::uint32_t LuminanceChromaImage::width() const noexcept
    {
        return width_;
    }

    std::size_t LuminanceChromaImage::paddedWidth() const noexcept
    {
        return static_cast<std::size_t>(width_) + kChromaTaps - 1;
    }

    std::span<Imf::Rgba> LuminanceChromaImage::row(const std::uint32_t y) noexcept
    {
        return std::span{rows_}.subspan(static_cast<std::size_t>(y) * paddedWidth(), paddedWidth());
    }

    std::vector<float> LuminanceChromaImage::reconstruct(const Imath::V3f &luminanceWeights) const
    {
        const std::size_t width = width_;
        const auto paddedWidth = this->paddedWidth();
        // Luminance/chroma rows with every chroma sample in place: even rows get the horizontal
        // filter over their padded copy (the pads repeat the first pixel and the last even one, as
        // FromYca::padTmpBuf does), odd rows are copied as they are.
        std::vector<Imf::Rgba> yca(width * height_);
        std::vector<Imf::Rgba> padded(paddedWidth);
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto source = std::span{rows_}.subspan(static_cast<std::size_t>(y) * paddedWidth, paddedWidth);
            std::ranges::copy(source, padded.begin());
            auto *target = yca.data() + static_cast<std::size_t>(y) * width;
            if ((y & 1U) != 0) {
                std::copy_n(padded.data() + kChromaPad, width, target);
                continue;
            }
            for (std::size_t pad = 0; pad < kChromaPad; ++pad) {
                padded[pad] = padded[kChromaPad];
                padded[width + kChromaPad + pad] = padded[width + kChromaPad - 2];
            }
            Imf::RgbaYca::reconstructChromaHoriz(static_cast<int>(width), padded.data(), target);
        }

        // RGBA rows: even rows convert directly, odd rows first reconstruct their chroma from the
        // 27 rows around them (even ones carry chroma; edge rows repeat as the library repeats them).
        std::vector<Imf::Rgba> rgba(width * height_);
        std::vector<Imf::Rgba> chroma(width);
        for (std::uint32_t y = 0; y < height_; ++y) {
            auto *target = rgba.data() + static_cast<std::size_t>(y) * width;
            if ((y & 1U) == 0) {
                Imf::RgbaYca::YCAtoRGBA(luminanceWeights, static_cast<int>(width),
                                        yca.data() + static_cast<std::size_t>(y) * width, target);
                continue;
            }
            std::array<const Imf::Rgba *, kChromaTaps> neighbours{};
            for (std::size_t tap = 0; tap < kChromaTaps; ++tap) {
                const auto row = clampRow(static_cast<std::int64_t>(y) - static_cast<std::int64_t>(kChromaPad) +
                                              static_cast<std::int64_t>(tap),
                                          height_);
                neighbours[tap] = yca.data() + row * width;
            }
            Imf::RgbaYca::reconstructChromaVert(static_cast<int>(width), neighbours.data(), chroma.data());
            Imf::RgbaYca::YCAtoRGBA(luminanceWeights, static_cast<int>(width), chroma.data(), target);
        }

        // The saturation fix looks at the rows above and below (edge rows repeated as above).
        std::vector<float> result(width * height_ * 4);
        std::vector<Imf::Rgba> fixed(width);
        for (std::uint32_t y = 0; y < height_; ++y) {
            const std::array<const Imf::Rgba *, 3> neighbours{
                rgba.data() + clampRow(static_cast<std::int64_t>(y) - 1, height_) * width,
                rgba.data() + static_cast<std::size_t>(y) * width,
                rgba.data() + clampRow(static_cast<std::int64_t>(y) + 1, height_) * width};
            Imf::RgbaYca::fixSaturation(luminanceWeights, static_cast<int>(width), neighbours.data(), fixed.data());
            const auto out = std::span{result}.subspan(static_cast<std::size_t>(y) * width * 4, width * 4);
            for (std::size_t x = 0; x < width; ++x) {
                out[x * 4] = static_cast<float>(fixed[x].r);
                out[x * 4 + 1] = static_cast<float>(fixed[x].g);
                out[x * 4 + 2] = static_cast<float>(fixed[x].b);
                out[x * 4 + 3] = static_cast<float>(fixed[x].a);
            }
        }
        return result;
    }

} // namespace pvdkit::exr
