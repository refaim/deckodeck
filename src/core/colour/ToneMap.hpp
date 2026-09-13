#pragma once

#include <array>

namespace pvdkit::core::colour::ToneMap
{

    inline constexpr float kTargetPeakNits = 100.0F;
    inline constexpr float kTargetBlackNits = 0.005F;
    using Rgb = std::array<float, 3>;

    /// BT.2390/BT.2408 Annex 5 EETF for a 100-nit, 0.005-nit-black reference display.
    class Eetf
    {
      public:
        explicit Eetf(float sourcePeakNits) noexcept;

        /// The normalized-PQ Hermite stage (E1 -> E2), before the specified black lift.
        [[nodiscard]] float compressionCurve(float normalizedPq) const noexcept;
        /// Maps absolute source luminance to target linear light normalized so 100 nits is 1.
        [[nodiscard]] float mapNits(float sourceNits) const noexcept;
        [[nodiscard]] float knee() const noexcept;
        [[nodiscard]] float kneeNits() const noexcept;

      private:
        float pqSourceBlack_;
        float pqSourceSpan_;
        float minimumLuminance_;
        float maximumLuminance_;
        float knee_;
    };

    /// BT.2390 Annex 5 maxRGB representation: the largest channel drives the EETF and one ratio
    /// scales the whole RGB triple, preserving chromaticity without per-channel clipping.
    [[nodiscard]] Rgb applyMaxRgb(const Eetf &curve, const Rgb &linearNits) noexcept;

} // namespace pvdkit::core::colour::ToneMap
