#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace pvdkit::exr
{

    /// The exact 16-bit PQ encoder for luminance in nits: `encode(nits)` returns the code
    /// `pqCode` returns (NaN and negatives are 0, 10000 nit and above are 65535), found by
    /// counting precomputed decision thresholds - the first float the pow path maps to each
    /// code - never by evaluating the PQ curve per pixel. Two `pow` calls per sample are what made
    /// a 4K decode take a second (plugins/exr/DESIGN.md, "Colour"). A bucket lookup on the float's
    /// bit pattern finds the first threshold the value can reach, and a fixed window of
    /// comparisons from there counts how many it passes: no data-dependent branch, so a picture's
    /// random values cost what a ramp costs (a binary search mispredicted its way to 27 ns per
    /// sample). Pure math that depends on nothing but the curve, so the plugin's factory builds
    /// one instance in `pvdInit` and every decode borrows it (no function-local static: MSVC >=
    /// 14.50 would import the synch API set for its guard). The same construction as
    /// `core::colour::SrgbOutputTables`.
    class PqCodeTables
    {
      public:
        /// The thresholds compared per lookup; at least the fullest bucket's population.
        static constexpr std::size_t kSearchWindow = 16;

        PqCodeTables();

        [[nodiscard]] std::uint16_t encode(float nits) const noexcept;
        /// The first float mapped to `code` (0 for code 0); exposed for the tests.
        [[nodiscard]] float threshold(std::uint16_t code) const noexcept;
        /// The most thresholds any bucket holds; exposed for the tests, which pin it under the window.
        [[nodiscard]] std::size_t widestBucket() const noexcept;

      private:
        // A bucket per value of a positive float's top 17 bits after the sign (exponent and 9
        // mantissa bits): 512 buckets per octave, so no bucket holds more than 14 thresholds (the
        // window is measured: 256 buckets need a 32-wide window and cost half as much again per
        // sample, 1024 buckets double the table for no gain); the encoder only looks up positive
        // finite values below 10000 nit.
        static constexpr unsigned kBucketShift = 14;
        static constexpr std::size_t kBucketCount = std::size_t{1} << (31 - kBucketShift);
        static constexpr std::size_t kCodeCount = 65'536;

        // The window may run past the last threshold; the padding is +inf, which no value passes.
        using Thresholds = std::array<float, kCodeCount - 1 + kSearchWindow>;
        using Buckets = std::array<std::uint16_t, kBucketCount + 1>;

        // Half a megabyte between them, on the heap so that the owner (a factory, a test) stays
        // small enough for any stack.
        std::unique_ptr<Thresholds> thresholds_;
        std::unique_ptr<Buckets> buckets_;
    };

} // namespace pvdkit::exr
