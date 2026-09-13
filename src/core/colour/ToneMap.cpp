#include "core/colour/ToneMap.hpp"

#include <algorithm>

#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour::ToneMap
{

    Eetf::Eetf(const float sourcePeakNits) noexcept
        : pqSourceBlack_(Transfer::nitsToPq(0.0F)),
          pqSourceSpan_(Transfer::nitsToPq(std::clamp(sourcePeakNits, 1.0F, 10'000.0F)) - pqSourceBlack_),
          minimumLuminance_((Transfer::nitsToPq(kTargetBlackNits) - pqSourceBlack_) / pqSourceSpan_),
          maximumLuminance_((Transfer::nitsToPq(kTargetPeakNits) - pqSourceBlack_) / pqSourceSpan_),
          knee_(1.5F * maximumLuminance_ - 0.5F)
    {
    }

    float Eetf::compressionCurve(const float normalizedPq) const noexcept
    {
        const auto value = std::clamp(normalizedPq, 0.0F, 1.0F);
        if (knee_ >= 1.0F || value < knee_) {
            return value;
        }

        // BT.2390-10 5.4.1 / BT.2408-9 Annex 5: the cubic Hermite knee has unit slope where it
        // meets the identity segment and zero slope at the mastering peak. The algebra and edge
        // handling were cross-checked against libjxl 0.11.2 Rec2408ToneMapper and vs-tonemap.
        const auto t = (value - knee_) / (1.0F - knee_);
        const auto squared = t * t;
        const auto cubed = squared * t;
        return (2.0F * cubed - 3.0F * squared + 1.0F) * knee_ + (cubed - 2.0F * squared + t) * (1.0F - knee_) +
               (-2.0F * cubed + 3.0F * squared) * maximumLuminance_;
    }

    float Eetf::mapNits(const float sourceNits) const noexcept
    {
        // BT.2390-10 5.4.1 normalizes within the complete source PQ range, including its
        // non-zero PQ code for a zero-nit mastering black.
        const auto normalized = (Transfer::nitsToPq(sourceNits) - pqSourceBlack_) / pqSourceSpan_;
        const auto compressed = compressionCurve(normalized);
        const auto distanceFromWhite = 1.0F - compressed;
        const auto squared = distanceFromWhite * distanceFromWhite;
        const auto lifted = compressed + minimumLuminance_ * squared * squared;
        return std::clamp(Transfer::pqToNits(lifted * pqSourceSpan_ + pqSourceBlack_) / kTargetPeakNits, 0.0F, 1.0F);
    }

    float Eetf::knee() const noexcept
    {
        return knee_;
    }

    float Eetf::kneeNits() const noexcept
    {
        return Transfer::pqToNits(knee_ * pqSourceSpan_ + pqSourceBlack_);
    }

    Rgb applyMaxRgb(const Eetf &curve, const Rgb &linearNits) noexcept
    {
        const auto driving = std::max({linearNits[0], linearNits[1], linearNits[2]});
        if (driving <= 0.0F) {
            constexpr auto black = kTargetBlackNits / kTargetPeakNits;
            return {black, black, black};
        }
        const auto scale = curve.mapNits(driving) / driving;
        return {linearNits[0] * scale, linearNits[1] * scale, linearNits[2] * scale};
    }

} // namespace pvdkit::core::colour::ToneMap
