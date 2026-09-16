#include "core/PqTables.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "core/Encode.hpp"

namespace pvdkit::exr
{
    namespace
    {

        constexpr float kCodeMaximum = 65'535.0F;

        /// The inverse of `pqCode`'s curve in double precision (SMPTE ST 2084 EOTF): the nits at
        /// which the encoded value is `code` (0..1).
        double pqToNits(const double code) noexcept
        {
            constexpr double m1 = 2610.0 / 16384.0;
            constexpr double m2 = 2523.0 / 4096.0 * 128.0;
            constexpr double c1 = 3424.0 / 4096.0;
            constexpr double c2 = 2413.0 / 4096.0 * 32.0;
            constexpr double c3 = 2392.0 / 4096.0 * 32.0;
            const auto raised = std::pow(code, 1.0 / m2);
            return 10'000.0 * std::pow(std::max(raised - c1, 0.0) / (c2 - c3 * raised), 1.0 / m1);
        }

        /// The first float `pqCode` maps to `code`, for code >= 1: the float nearest the exact
        /// crossing (the double inverse of the decision boundary), nudged by one ULP where the
        /// rounding of the crossing put it on the wrong side. The curve is monotone in double
        /// precision at the resolution of a code, so both walks end within a step or two.
        float outputThreshold(const std::uint16_t code) noexcept
        {
            const auto boundary = (static_cast<double>(code) - 0.5) / static_cast<double>(kCodeMaximum);
            auto threshold = static_cast<float>(pqToNits(boundary));
            while (pqCode(threshold) >= code) {
                threshold = std::nextafter(threshold, 0.0F);
            }
            while (pqCode(threshold) < code) {
                threshold = std::nextafter(threshold, kMaximumPeakNits);
            }
            return threshold;
        }

        /// The lowest positive float of a bucket, which is the bucket index shifted back.
        float bucketBoundary(const std::uint32_t bucket, const unsigned shift) noexcept
        {
            return std::bit_cast<float>(bucket << shift);
        }

    } // namespace

    PqCodeTables::PqCodeTables() : thresholds_(std::make_unique<Thresholds>()), buckets_(std::make_unique<Buckets>())
    {
        auto &thresholds = *thresholds_;
        auto &buckets = *buckets_;
        constexpr std::size_t thresholdCount = kCodeCount - 1;
        for (std::size_t index = 0; index < thresholdCount; ++index) {
            thresholds[index] = outputThreshold(static_cast<std::uint16_t>(index + 1));
        }
        std::fill(thresholds.begin() + thresholdCount, thresholds.end(), std::numeric_limits<float>::infinity());
        // buckets[b] is the number of thresholds at or below the bucket's lower boundary, i.e. the
        // index of the first threshold a value of the bucket can pass: one merge pass over the
        // ascending thresholds (positive floats order like their bit patterns, so the boundaries
        // ascend with b).
        std::size_t count = 0;
        for (std::uint32_t bucket = 0; bucket < kBucketCount; ++bucket) {
            const auto boundary = bucketBoundary(bucket, kBucketShift);
            while (count < thresholdCount && thresholds[count] <= boundary) {
                ++count;
            }
            buckets[bucket] = static_cast<std::uint16_t>(count);
        }
        buckets[kBucketCount] = static_cast<std::uint16_t>(thresholdCount);
    }

    std::uint16_t PqCodeTables::encode(const float nits) const noexcept
    {
        // The negated comparison also sends NaN to code 0, the definition's answer.
        if (!(nits > 0.0F)) {
            return 0;
        }
        if (nits >= kMaximumPeakNits) {
            return static_cast<std::uint16_t>(kCodeMaximum);
        }
        const auto bucket = std::bit_cast<std::uint32_t>(nits) >> kBucketShift;
        const std::size_t first = (*buckets_)[bucket];
        const auto &thresholds = *thresholds_;
        // The code is the number of thresholds the value reaches: those before the bucket, plus
        // those of the window it passes. The window runs to the next bucket's thresholds (or the
        // +inf padding), which the value cannot pass, so the fixed trip count reads the same answer
        // an exact search would; no curve value is interpolated. Every iteration is a compare and
        // an add, which the compiler turns into vector lanes.
        std::uint32_t passed = 0;
        for (std::size_t offset = 0; offset < kSearchWindow; ++offset) {
            passed += thresholds[first + offset] <= nits ? 1U : 0U;
        }
        return static_cast<std::uint16_t>(first + passed);
    }

    float PqCodeTables::threshold(const std::uint16_t code) const noexcept
    {
        return code == 0 ? 0.0F : (*thresholds_)[static_cast<std::size_t>(code) - 1];
    }

    std::size_t PqCodeTables::widestBucket() const noexcept
    {
        std::size_t widest = 0;
        const auto &buckets = *buckets_;
        for (std::size_t bucket = 0; bucket < kBucketCount; ++bucket) {
            widest = std::max(widest, static_cast<std::size_t>(buckets[bucket + 1] - buckets[bucket]));
        }
        return widest;
    }

} // namespace pvdkit::exr
