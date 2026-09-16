#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/PqTables.hpp"
#include "core/colour/Primaries.hpp"

namespace pvdkit::exr
{

    /// The sRGB viewing convention for scene-linear data: 1.0 is 100 nit unless the file says otherwise.
    inline constexpr float kDefaultWhiteNits = 100.0F;
    /// The tone-mapping peak is kept within what BT.2390 and the PQ curve can express.
    inline constexpr float kMinimumPeakNits = 100.0F;
    inline constexpr float kMaximumPeakNits = 10'000.0F;
    /// The fraction of display-window pixels at or below the peak.
    inline constexpr double kPeakPercentile = 0.9999;

    /// Nits per scene-linear unit: the `whiteLuminance` attribute when it is finite and positive,
    /// otherwise the 100-nit default.
    [[nodiscard]] float nitsPerUnit(std::optional<float> whiteLuminance) noexcept;

    /// The 16-bit ST 2084 code of a luminance in nits: NaN, negative and -inf are black, +inf and
    /// anything above 10000 nit saturate. This is the definition (two `pow` calls); the pixel loop
    /// uses `PqCodeTables`, which answers the same code by threshold search.
    [[nodiscard]] std::uint16_t pqCode(float nits) noexcept;

    /// Counts pixels by the PQ code of their luminance so the peak can be read as a percentile.
    class LuminanceHistogram
    {
      public:
        LuminanceHistogram();

        void add(std::uint16_t code) noexcept;
        [[nodiscard]] std::uint64_t count() const noexcept;
        /// The smallest code such that at least `rank` of the `displayPixels` pixels are at or below
        /// it, pixels never added counting as code 0.
        [[nodiscard]] std::uint16_t codeAtRank(std::uint64_t rank, std::uint64_t displayPixels) const noexcept;

        [[nodiscard]] bool operator==(const LuminanceHistogram &) const = default;

      private:
        std::vector<std::uint64_t> bins_;
        std::uint64_t count_ = 0;
    };

    /// The BT.2390 source peak for the picture: the 99.99th-percentile luminance of the display
    /// window's pixels (those outside the data window are black), clamped to [100, 10000] nit.
    [[nodiscard]] float peakNits(const LuminanceHistogram &histogram, std::uint64_t displayPixels) noexcept;

    struct EncodeParams
    {
        float nitsPerUnit = kDefaultWhiteNits;
        bool alpha = false; ///< The fourth float of every pixel is associated (premultiplied) alpha.
        bool grey = false;  ///< Only the first float carries light; it feeds all three channels.
        core::colour::Primaries::Rgb luminance{0.2126F, 0.7152F, 0.0722F}; ///< Y coefficients of the file's primaries.
        const PqCodeTables &tables;                                        ///< The plugin's exact encoder.
    };

    /// Turns interleaved scene-linear RGBA floats (four per pixel; the fourth is ignored without
    /// alpha) into BGRA64 PQ codes with straight alpha, sanitizing NaN/inf/negative samples, and
    /// adds every pixel's luminance to the histogram. `bgra` holds four codes per pixel.
    void encodePixels(std::span<const float> rgba, const EncodeParams &params, std::span<std::uint16_t> bgra,
                      LuminanceHistogram &histogram) noexcept;

} // namespace pvdkit::exr
