#include "core/Encode.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/colour/Transfer.hpp"

namespace pvdkit::exr
{
    namespace
    {

        constexpr std::size_t kCodeCount = 65'536;
        constexpr float kCodeMaximum = 65'535.0F;

        /// NaN, negative and -inf become 0; +inf passes (the PQ curve saturates it).
        float sanitize(const float value) noexcept
        {
            return value > 0.0F ? value : 0.0F;
        }

        /// Rounds coverage to the nearest code, halves up: the same answer as `lround` for the
        /// non-negative range (the sum is exact below 2^17), without a libm call per pixel. The
        /// rounding check objects to the pattern for negative operands; `sanitize` rules those out.
        std::uint16_t alphaCode(const float alpha) noexcept
        {
            // NOLINTNEXTLINE(bugprone-incorrect-roundings)
            return static_cast<std::uint16_t>(std::min(sanitize(alpha), 1.0F) * kCodeMaximum + 0.5F);
        }

    } // namespace

    float nitsPerUnit(const std::optional<float> whiteLuminance) noexcept
    {
        if (!whiteLuminance || !std::isfinite(*whiteLuminance) || *whiteLuminance <= 0.0F) {
            return kDefaultWhiteNits;
        }
        return *whiteLuminance;
    }

    std::uint16_t pqCode(const float nits) noexcept
    {
        // SMPTE ST 2084 inverse EOTF in double precision: the float curve of the shared Transfer
        // module is what the presentation decodes with, but as a definition of the 16-bit code it
        // is noisy at the last code (pow(base, 78.84) amplifies the base's rounding to half a code),
        // so the code is defined by the exact curve; NaN, negative and -inf are sanitized to 0 first.
        constexpr double m1 = 2610.0 / 16384.0;
        constexpr double m2 = 2523.0 / 4096.0 * 128.0;
        constexpr double c1 = 3424.0 / 4096.0;
        constexpr double c2 = 2413.0 / 4096.0 * 32.0;
        constexpr double c3 = 2392.0 / 4096.0 * 32.0;
        const auto luminance = std::min(static_cast<double>(sanitize(nits)), static_cast<double>(kMaximumPeakNits));
        const auto raised = std::pow(luminance / static_cast<double>(kMaximumPeakNits), m1);
        const auto encoded = std::pow((c1 + c2 * raised) / (1.0 + c3 * raised), m2);
        return static_cast<std::uint16_t>(std::lround(encoded * static_cast<double>(kCodeMaximum)));
    }

    LuminanceHistogram::LuminanceHistogram() : bins_(kCodeCount)
    {
    }

    void LuminanceHistogram::add(const std::uint16_t code) noexcept
    {
        ++bins_[code];
        ++count_;
    }

    std::uint64_t LuminanceHistogram::count() const noexcept
    {
        return count_;
    }

    std::uint16_t LuminanceHistogram::codeAtRank(const std::uint64_t rank,
                                                 const std::uint64_t displayPixels) const noexcept
    {
        // Pixels outside the data window were never added and count as black (code 0).
        std::uint64_t cumulative = displayPixels - std::min(count_, displayPixels);
        for (std::size_t code = 0; code + 1 < bins_.size(); ++code) {
            cumulative += bins_[code];
            if (cumulative >= rank) {
                return static_cast<std::uint16_t>(code);
            }
        }
        return static_cast<std::uint16_t>(bins_.size() - 1);
    }

    float peakNits(const LuminanceHistogram &histogram, const std::uint64_t displayPixels) noexcept
    {
        const auto rank = static_cast<std::uint64_t>(std::ceil(kPeakPercentile * static_cast<double>(displayPixels)));
        const auto code = histogram.codeAtRank(rank, displayPixels);
        const auto nits = core::colour::Transfer::pqToNits(static_cast<float>(code) / kCodeMaximum);
        return std::clamp(nits, kMinimumPeakNits, kMaximumPeakNits);
    }

    void encodePixels(const std::span<const float> rgba, const EncodeParams &params,
                      const std::span<std::uint16_t> bgra, LuminanceHistogram &histogram) noexcept
    {
        const auto pixelCount = rgba.size() / 4;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const auto source = rgba.subspan(pixel * 4, 4);
            float red = sanitize(source[0]) * params.nitsPerUnit;
            float green = params.grey ? red : sanitize(source[1]) * params.nitsPerUnit;
            float blue = params.grey ? red : sanitize(source[2]) * params.nitsPerUnit;
            std::uint16_t alpha = 65'535;
            if (params.alpha) {
                // Associated alpha by OpenEXR convention: divide out a positive coverage below one.
                // Zero coverage keeps its colour (additive light); above one is clamped, no division.
                const auto coverage = std::min(sanitize(source[3]), 1.0F);
                if (coverage > 0.0F) {
                    red /= coverage;
                    green /= coverage;
                    blue /= coverage;
                }
                alpha = alphaCode(source[3]);
            }
            const auto target = bgra.subspan(pixel * 4, 4);
            target[0] = params.tables.encode(blue);
            target[1] = params.tables.encode(green);
            target[2] = params.tables.encode(red);
            target[3] = alpha;
            histogram.add(params.tables.encode(params.luminance[0] * red + params.luminance[1] * green +
                                               params.luminance[2] * blue));
        }
    }

} // namespace pvdkit::exr
