#pragma once

// The scalar reference of the shared presentation's pixel order, kept in the tests only: one
// pixel in straightforward float arithmetic, no table other than the shared sRGB quantiser
// (whose own exactness is proven by the exhaustive diagnostic in tests/core/colour). The order is
// the production one (docs/ARCHITECTURE.md section 3.7): transfer decode of each 16-bit code,
// the HLG OOTF, the BT.2390 EETF on max(R, G, B) - *before* the primaries matrix for PQ over a
// coded primaries set, *after* the conversion to BT.709 for HLG and for explicit chromaticities -
// and the exact sRGB quantiser. The EETF is evaluated per pixel here in both places, while
// Presentation reads the before-the-matrix one from a 65,536-entry gain table indexed by the
// maximum channel code. The two must agree bit for bit on every pixel; PipelineTests and the
// AVIF adapter tests pin that.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "core/IDecoder.hpp"
#include "core/colour/Pipeline.hpp"
#include "core/colour/Primaries.hpp"
#include "core/colour/ToneMap.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::tests
{

    class PresentationReference
    {
      public:
        PresentationReference(const core::Cicp &cicp, const std::optional<float> masteringPeakNits,
                              const std::optional<core::colour::Primaries::Chromaticities> &chromaticities,
                              const core::colour::SrgbOutputTables &outputTables)
            : PresentationReference(cicp, masteringPeakNits, usable(chromaticities), outputTables, Filtered{})
        {
        }

        /// The BGR codes of one pixel given as BGR codes, as Presentation::apply stores them.
        [[nodiscard]] std::array<std::uint16_t, 3> convert(const std::uint16_t blue, const std::uint16_t green,
                                                           const std::uint16_t red) const
        {
            using core::colour::Primaries::Rgb;
            Rgb linear{decode(red), decode(green), decode(blue)};
            if (transfer_ == 18) {
                linear = hlgDisplayNits(linear);
            }
            if (toneMapInSource_) {
                linear = core::colour::ToneMap::applyMaxRgb(eetf_, linear);
            }
            linear = core::colour::Primaries::apply(matrix_, linear);
            if (toneMapAfterMatrix_) {
                linear = core::colour::ToneMap::applyMaxRgb(eetf_, linear);
            }
            return {outputTables_.quantize(linear[2]), outputTables_.quantize(linear[1]),
                    outputTables_.quantize(linear[0])};
        }

        /// True when Presentation reads the EETF from its gain table (PQ over coded primaries).
        [[nodiscard]] bool toneMapsInSource() const noexcept
        {
            return toneMapInSource_;
        }

      private:
        struct Filtered
        {
        };

        /// The set a Presentation converts through: an unusable set is ignored like the production
        /// constructor ignores it (the coded primaries stand).
        [[nodiscard]] static std::optional<core::colour::Primaries::Chromaticities> usable(
            const std::optional<core::colour::Primaries::Chromaticities> &chromaticities)
        {
            return chromaticities && core::colour::Primaries::isUsable(*chromaticities) ? chromaticities : std::nullopt;
        }

        PresentationReference(const core::Cicp &cicp, const std::optional<float> masteringPeakNits,
                              const std::optional<core::colour::Primaries::Chromaticities> &chromaticities,
                              const core::colour::SrgbOutputTables &outputTables, Filtered)
            : transfer_(cicp.transfer), toneMapInSource_(cicp.transfer == 16 && !chromaticities.has_value()),
              toneMapAfterMatrix_(core::colour::Transfer::isHdr(cicp.transfer) && !toneMapInSource_),
              matrix_(chromaticities ? core::colour::Primaries::toSrgb(*chromaticities)
                                     : core::colour::Primaries::toSrgb(cicp.primaries)),
              luminance_(chromaticities ? core::colour::Primaries::luminanceCoefficients(*chromaticities)
                                        : core::colour::Primaries::luminanceCoefficients(cicp.primaries)),
              eetf_(core::colour::sourcePeakNits(cicp.transfer, masteringPeakNits)), outputTables_(outputTables)
        {
        }

        [[nodiscard]] float decode(const std::uint16_t code) const
        {
            return core::colour::Transfer::toLinear(transfer_, static_cast<float>(code) / 65'535.0F);
        }

        /// ITU-R BT.2100 HLG OOTF for the 1000-nit reference display (alpha = 1000, gamma = 1.2),
        /// as Pipeline.cpp applies it: Y_s^(gamma - 1) scales every channel.
        [[nodiscard]] core::colour::Primaries::Rgb hlgDisplayNits(const core::colour::Primaries::Rgb &scene) const
        {
            const auto luminance = luminance_[0] * scene[0] + luminance_[1] * scene[1] + luminance_[2] * scene[2];
            if (luminance <= 0.0F) {
                return {};
            }
            const auto scale = 1'000.0F * std::pow(luminance, 0.2F);
            return {scene[0] * scale, scene[1] * scale, scene[2] * scale};
        }

        std::uint16_t transfer_;
        bool toneMapInSource_;
        bool toneMapAfterMatrix_;
        core::colour::Primaries::Matrix3 matrix_;
        core::colour::Primaries::Rgb luminance_;
        core::colour::ToneMap::Eetf eetf_;
        const core::colour::SrgbOutputTables &outputTables_;
    };

} // namespace pvdkit::tests
