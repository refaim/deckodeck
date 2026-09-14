#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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

        [[nodiscard]] std::uint16_t quantize(float linear) const noexcept;

      private:
        // A coarse table over [0, 1) narrows each threshold search to one bucket; a power of two so
        // that bucket boundaries and the bucket index of an input are computed exactly.
        static constexpr std::size_t kBucketCount = 65'536;

        std::array<float, 65'535> thresholds_{};
        std::array<std::uint16_t, kBucketCount + 1> buckets_{};
    };

    class Presentation
    {
      public:
        /// `outputTables` is borrowed for the lifetime of this object; the caller keeps it alive
        /// (the composition root owns it and outlives every session, ARCHITECTURE section 2).
        Presentation(const Cicp &cicp, std::optional<float> masteringPeakNits, const SrgbOutputTables &outputTables);

        Presentation(const Presentation &) = delete;
        Presentation &operator=(const Presentation &) = delete;
        Presentation(Presentation &&) = delete;
        Presentation &operator=(Presentation &&) = delete;

        [[nodiscard]] static bool needed(const Cicp &cicp) noexcept;
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

      private:
        using TransferLut = std::array<float, 65'536>;

        [[nodiscard]] std::array<std::uint16_t, 3> convert(std::uint16_t blue, std::uint16_t green,
                                                           std::uint16_t red) const noexcept;

        bool active_;
        bool hlg_;
        bool hdr_;
        float sourcePeakNits_;
        Primaries::Matrix3 primaries_;
        Primaries::Rgb sourceLuminance_;
        ToneMap::Eetf toneMap_;
        const SrgbOutputTables &outputTables_;
        std::unique_ptr<TransferLut> transferLut_;
    };

} // namespace pvdkit::core::colour
