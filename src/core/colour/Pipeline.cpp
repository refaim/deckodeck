#include "core/colour/Pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour
{
    namespace
    {

        constexpr float kDefaultHdrPeakNits = 1'000.0F;

        Primaries::Rgb hlgDisplayNits(const Primaries::Rgb &scene, const Primaries::Rgb &luminanceCoefficients) noexcept
        {
            const auto luminance = luminanceCoefficients[0] * scene[0] + luminanceCoefficients[1] * scene[1] +
                                   luminanceCoefficients[2] * scene[2];
            if (luminance <= 0.0F) {
                return {};
            }
            // ITU-R BT.2100 HLG OOTF for the 1000-nit reference display: alpha=1000 and system
            // gamma=1.2. Applying Y_s^(gamma-1) to every channel preserves the scene chromaticity.
            const auto scale = kDefaultHdrPeakNits * std::pow(luminance, 0.2F);
            return {scene[0] * scale, scene[1] * scale, scene[2] * scale};
        }

        std::uint16_t quantize(const float linear) noexcept
        {
            const auto encoded = std::clamp(Transfer::linearToSrgb(linear), 0.0F, 1.0F);
            return static_cast<std::uint16_t>(std::lround(encoded * std::numeric_limits<std::uint16_t>::max()));
        }

    } // namespace

    float sourcePeakNits(const std::uint16_t transfer, const std::optional<float> masteringPeakNits) noexcept
    {
        if (transfer == 18) {
            return kDefaultHdrPeakNits;
        }
        if (!masteringPeakNits) {
            return kDefaultHdrPeakNits;
        }
        if (!std::isfinite(*masteringPeakNits) || *masteringPeakNits <= 0.0F) {
            return kDefaultHdrPeakNits;
        }
        return std::min(*masteringPeakNits, 10'000.0F);
    }

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits)
        : active_(needed(cicp)), hlg_(cicp.transfer == 18), hdr_(Transfer::isHdr(cicp.transfer)),
          sourcePeakNits_(colour::sourcePeakNits(cicp.transfer, masteringPeakNits)),
          primaries_(Primaries::toSrgb(cicp.primaries)),
          sourceLuminance_(Primaries::luminanceCoefficients(cicp.primaries)), toneMap_(sourcePeakNits_)
    {
        if (!active_) {
            return;
        }
        transferLut_ = std::make_unique<TransferLut>();
        for (std::size_t sample = 0; sample < transferLut_->size(); ++sample) {
            const auto encoded = static_cast<float>(sample) / std::numeric_limits<std::uint16_t>::max();
            (*transferLut_)[sample] = Transfer::toLinear(cicp.transfer, encoded);
        }
    }

    bool Presentation::needed(const Cicp &cicp) noexcept
    {
        const bool transferConversion =
            cicp.transfer == 4 || cicp.transfer == 5 || cicp.transfer == 8 || Transfer::isHdr(cicp.transfer);
        return !Primaries::isIdentity(cicp.primaries) || transferConversion;
    }

    void Presentation::apply(const std::span<std::uint16_t> bgraRow) const noexcept
    {
        if (!active_) {
            return;
        }

        for (std::size_t offset = 0; offset < bgraRow.size(); offset += 4) {
            const auto converted = convert(bgraRow[offset], bgraRow[offset + 1], bgraRow[offset + 2]);
            bgraRow[offset] = converted[0];
            bgraRow[offset + 1] = converted[1];
            bgraRow[offset + 2] = converted[2];
        }
    }

    void Presentation::apply(const std::span<std::byte> bgraRow) const noexcept
    {
        if (!active_) {
            return;
        }

        const auto load = [&](const std::size_t offset) {
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bgraRow[offset])) |
                   static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bgraRow[offset + 1]) << 8U);
        };
        const auto store = [&](const std::size_t offset, const std::uint16_t sample) {
            bgraRow[offset] = std::byte{static_cast<std::uint8_t>(sample & 0xffU)};
            bgraRow[offset + 1] = std::byte{static_cast<std::uint8_t>(sample >> 8U)};
        };

        for (std::size_t offset = 0; offset < bgraRow.size(); offset += 8) {
            const auto converted = convert(load(offset), load(offset + 2), load(offset + 4));
            store(offset, converted[0]);
            store(offset + 2, converted[1]);
            store(offset + 4, converted[2]);
        }
    }

    std::array<std::uint16_t, 3> Presentation::convert(const std::uint16_t blue, const std::uint16_t green,
                                                       const std::uint16_t red) const noexcept
    {
        Primaries::Rgb linear{(*transferLut_)[red], (*transferLut_)[green], (*transferLut_)[blue]};
        if (hlg_) {
            linear = hlgDisplayNits(linear, sourceLuminance_);
        }
        linear = Primaries::apply(primaries_, linear);
        if (hdr_) {
            linear = ToneMap::applyMaxRgb(toneMap_, linear);
        }
        return {quantize(linear[2]), quantize(linear[1]), quantize(linear[0])};
    }

    float Presentation::sourcePeakNits() const noexcept
    {
        return sourcePeakNits_;
    }

} // namespace pvdkit::core::colour
