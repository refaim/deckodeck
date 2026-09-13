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

    class Presentation
    {
      public:
        Presentation(const Cicp &cicp, std::optional<float> masteringPeakNits);

        Presentation(Presentation &&) noexcept = default;
        Presentation &operator=(Presentation &&) noexcept = default;
        Presentation(const Presentation &) = delete;
        Presentation &operator=(const Presentation &) = delete;

        [[nodiscard]] static bool needed(const Cicp &cicp) noexcept;
        /// Converts one tightly packed BGRA64 row in place. Alpha is copied through unchanged.
        void apply(std::span<std::uint16_t> bgraRow) const noexcept;
        /// Byte-span bridge for PixelBuffer, whose storage is byte-owned by the shared core.
        void apply(std::span<std::byte> bgraRow) const noexcept;
        [[nodiscard]] float sourcePeakNits() const noexcept;

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
        std::unique_ptr<TransferLut> transferLut_;
    };

} // namespace pvdkit::core::colour
