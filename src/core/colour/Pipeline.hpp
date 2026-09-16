#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>

#include "core/IDecoder.hpp"
#include "core/colour/Primaries.hpp"
#include "core/colour/ToneMap.hpp"

namespace pvdkit::core::colour
{

    /// Resolves the HDR source peak used by the presentation pipeline. HLG uses its fixed
    /// 1000-nit reference display; absent or invalid PQ metadata also falls back to 1000 nits.
    [[nodiscard]] float sourcePeakNits(std::uint16_t transfer, std::optional<float> masteringPeakNits) noexcept;

    /// The exact sRGB output quantizer. For every linear float it returns the 16-bit code the
    /// exact OETF-plus-lround computation returns, found by searching precomputed decision
    /// thresholds (never by interpolating an OETF value). Pure, immutable math that depends on no
    /// Cicp, so a plugin's composition root builds one instance per plugin and every Presentation
    /// of that plugin borrows it by reference (docs/ARCHITECTURE.md section 7). Nothing here is a
    /// function-local static: the guarded initialisation of one would make MSVC >= 14.50 import
    /// api-ms-win-core-synch-l1-2-0.dll, and a plugin imports KERNEL32.dll only.
    class SrgbOutputTables
    {
      public:
        SrgbOutputTables();

        /// Inline (defined below the class): it runs three times per pixel.
        [[nodiscard]] std::uint16_t quantize(float linear) const noexcept;

      private:
        // A coarse table over [0, 1) narrows each threshold search to one bucket; a power of two so
        // that bucket boundaries and the bucket index of an input are computed exactly.
        static constexpr std::size_t kBucketCount = 65'536;
        // No bucket holds more than kWindow thresholds (the densest region is the sRGB linear
        // segment, 12.92 x 65535 / 65536 = 12.92 per bucket; the widest bucket built holds 13),
        // so a fixed window of kWindow comparisons from the bucket's first threshold counts every
        // threshold at or below the input - a loop with no data-dependent trip count, which the
        // compiler vectorises. The array is padded with +infinity so the window never reads past
        // the last threshold. The exhaustive diagnostic in PipelineTests (every float in [0, 1])
        // is the proof that the window is wide enough.
        static constexpr std::size_t kWindow = 16;

        std::array<float, 65'535 + kWindow> thresholds_{};
        std::array<std::uint16_t, kBucketCount + 1> buckets_{};
    };

    inline std::uint16_t SrgbOutputTables::quantize(const float linear) const noexcept
    {
        // The negated comparison also sends NaN to code 0, the exact path's answer on this CRT.
        if (!(linear > 0.0F)) {
            return 0;
        }
        if (linear >= 1.0F) {
            return std::numeric_limits<std::uint16_t>::max();
        }

        // The code is the number of thresholds at or below the input: those before the bucket
        // (buckets_[bucket]) plus those in the bucket, counted over the fixed window (every
        // threshold beyond the bucket is above its upper boundary and so above the input). The
        // bucket only narrows the exact threshold count; no OETF value is interpolated.
        const auto bucket = static_cast<std::size_t>(linear * static_cast<float>(kBucketCount));
        const std::size_t first = buckets_[bucket];
        std::size_t count = first;
        for (std::size_t index = 0; index < kWindow; ++index) {
            count += thresholds_[first + index] <= linear ? 1U : 0U;
        }
        return static_cast<std::uint16_t>(count);
    }

    /// The per-session presentation: 16-bit BGRA codes of a CICP-described source to 16-bit sRGB.
    /// Per pixel: the transfer LUT (linear light, absolute nits for PQ), the HLG OOTF, the
    /// BT.2390 EETF on max(R, G, B), the primaries matrix, the exact sRGB quantiser - the EETF in
    /// one of two places (docs/ARCHITECTURE.md section 3.7):
    /// - PQ over any coded primaries set (an H.273 code, known or not, identity included - every
    ///   one a display-like RGB container - and no usable explicit chromaticities): *before* the
    ///   matrix, in the source container, as a 65,536-entry gain table indexed by the maximum
    ///   channel code. The LUT is monotone non-decreasing, so the largest of the three LUT values
    ///   is the LUT at the largest code, and the gain of that value is a function of the code
    ///   alone - the same float arithmetic as evaluating the curve per pixel, no interpolation.
    /// - HLG (its OOTF scales by the pixel's own luminance) and PQ over usable explicit
    ///   chromaticities (CIE XYZ, ACES AP0/AP1, custom sets: containers whose largest channel is
    ///   not a brightness measure): *after* the conversion to BT.709, evaluated per pixel, as in
    ///   Tasks 16-26, so that a picture presents alike whichever such container holds it.
    /// The tables are built on the calling thread; a session starts no thread at construction.
    class Presentation
    {
      public:
        /// `outputTables` is borrowed for the lifetime of this object; the caller keeps it alive
        /// (the composition root owns it and outlives every session, ARCHITECTURE section 2).
        Presentation(const Cicp &cicp, std::optional<float> masteringPeakNits, const SrgbOutputTables &outputTables);
        /// The same over explicit chromaticities (`ImageMeta::chromaticities`): when set and usable
        /// (`Primaries::isUsable`) they replace the coded primaries and the matrix is derived from
        /// them at run time; a set that is not usable is ignored (the coded primaries stand).
        Presentation(const Cicp &cicp, std::optional<float> masteringPeakNits,
                     const std::optional<Primaries::Chromaticities> &chromaticities,
                     const SrgbOutputTables &outputTables);

        Presentation(const Presentation &) = delete;
        Presentation &operator=(const Presentation &) = delete;
        Presentation(Presentation &&) = delete;
        Presentation &operator=(Presentation &&) = delete;

        [[nodiscard]] static bool needed(const Cicp &cicp) noexcept;
        /// Usable explicit chromaticities always need presentation: they are, by definition, not
        /// sRGB. A set that is not usable is ignored here as in the constructor.
        [[nodiscard]] static bool needed(const Cicp &cicp,
                                         const std::optional<Primaries::Chromaticities> &chromaticities) noexcept;
        /// Converts one tightly packed BGRA64 row in place. Alpha is copied through unchanged.
        void apply(std::span<std::uint16_t> bgraRow) const noexcept;
        /// Byte-span bridge for PixelBuffer, whose storage is byte-owned by the shared core.
        void apply(std::span<std::byte> bgraRow) const noexcept;
        /// Converts a whole tightly packed BGRA64 image (`height` rows of `pitchBytes`) in place.
        /// Images of at least 256 Ki pixels are split into min(clamp(maxThreads, 1, 4), height)
        /// disjoint row bands: the last band runs on the calling thread, the others on
        /// std::thread workers that are joined before the call returns, on every path.
        void applyImage(std::span<std::byte> pixels, std::uint32_t pitchBytes, std::uint32_t height,
                        unsigned maxThreads) const;
        [[nodiscard]] float sourcePeakNits() const noexcept;
        [[nodiscard]] const SrgbOutputTables &outputTables() const noexcept;
        /// The BT.2390 gain by maximum channel code: 65,536 entries for a PQ session over coded
        /// primaries (the table path), empty for every other (SDR, wide-gamut-only and HLG
        /// sessions and PQ over explicit chromaticities never build it). Exposed so the tests can
        /// pin which path a session takes and what the table holds.
        [[nodiscard]] std::span<const float> toneMapGains() const noexcept;

      private:
        using Table = std::array<float, 65'536>;

        struct Usable
        {
        };
        /// The members' initialisation, over chromaticities already checked (or absent).
        Presentation(const Cicp &cicp, std::optional<float> masteringPeakNits,
                     const std::optional<Primaries::Chromaticities> &chromaticities,
                     const SrgbOutputTables &outputTables, Usable);

        /// Converts `pixelCount` pixels read through `load(pixel, channel)` (channel 0 = blue,
        /// 1 = green, 2 = red; a 16-bit code) and written through `store(pixel, channel, code)`, in
        /// blocks of a few dozen pixels and three passes over structure-of-arrays stack buffers:
        /// decode (LUT, OOTF, the table path's gain), the matrix (a call-free, branch-free loop the
        /// compiler vectorises), the scalar path's EETF and the quantiser.
        template <class Load, class Store>
        void convertPixels(std::size_t pixelCount, const Load &load, const Store &store) const noexcept;

        bool active_;
        bool hlg_;
        /// HLG and PQ over explicit chromaticities: the EETF runs per pixel after the matrix.
        bool toneMapAfterMatrix_;
        float sourcePeakNits_;
        Primaries::Matrix3 primaries_;
        Primaries::Rgb sourceLuminance_;
        ToneMap::Eetf toneMap_;
        const SrgbOutputTables &outputTables_;
        std::unique_ptr<Table> transferLut_;
        std::unique_ptr<Table> toneGain_;
    };

} // namespace pvdkit::core::colour
