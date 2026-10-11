#include "core/colour/Pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour
{
    namespace
    {

        constexpr float kDefaultHdrPeakNits = 1'000.0F;
        constexpr auto kCodeMaximum = std::numeric_limits<std::uint16_t>::max();
        /// What the EETF makes of a pixel with no light in any channel (ToneMap::applyMaxRgb):
        /// the target display's black, before the matrix.
        constexpr float kTargetBlack = ToneMap::kTargetBlackNits / ToneMap::kTargetPeakNits;

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

        /// The band workers of Presentation::applyImage: std::thread objects joined when this goes
        /// out of scope, on the normal path and during unwinding (a std::thread constructor that
        /// fails throws std::system_error after the earlier workers have started). std::jthread
        /// would join the same way, but its stop_token state is built on atomic wait/notify, for
        /// which MSVC >= 14.50 imports api-ms-win-core-synch-l1-2-0.dll; a plugin imports
        /// KERNEL32.dll only (docs/ARCHITECTURE.md section 7). No stop request exists here: a
        /// band either runs to completion or was never started.
        class BandWorkers
        {
          public:
            explicit BandWorkers(const std::size_t capacity)
            {
                workers_.reserve(capacity);
            }

            ~BandWorkers()
            {
                for (auto &worker : workers_) {
                    worker.join();
                }
            }

            BandWorkers(const BandWorkers &) = delete;
            BandWorkers &operator=(const BandWorkers &) = delete;
            BandWorkers(BandWorkers &&) = delete;
            BandWorkers &operator=(BandWorkers &&) = delete;

            template <class Task> void start(Task &&task)
            {
                workers_.emplace_back(std::forward<Task>(task));
            }

          private:
            std::vector<std::thread> workers_;
        };

        using ColourTable = std::array<float, 65'536>;

        std::size_t transferSlot(const std::uint16_t characteristics) noexcept
        {
            switch (characteristics) {
            case 1:
            case 6:
            case 14:
            case 15:
                return 0;
            case 4:
                return 1;
            case 5:
                return 2;
            case 8:
                return 3;
            case 16:
                return 4;
            case 18:
                return 5;
            default:
                return 6;
            }
        }

        std::unique_ptr<const ColourTable> makeGainTable(const float peakNits, const std::span<const float> transfer)
        {
            auto values = std::make_unique<ColourTable>();
            const ToneMap::Eetf toneMap{peakNits};
            for (std::size_t code = 0; code < values->size(); ++code) {
                const auto linear = transfer[code];
                (*values)[code] = linear > 0.0F ? toneMap.mapNits(linear) / linear : 0.0F;
            }
            return values;
        }

        std::uint16_t exactQuantize(const float linear) noexcept
        {
            const auto encoded = std::clamp(Transfer::linearToSrgb(linear), 0.0F, 1.0F);
            return static_cast<std::uint16_t>(std::lround(encoded * kCodeMaximum));
        }

        float outputThreshold(const std::uint16_t code) noexcept
        {
            // The first float which the exact OETF-plus-lround path maps to `code`. The inverse
            // OETF of the decision boundary lands within a few ULPs of it; walk in whichever
            // direction the float rounding of the forward path moved the crossing. An ascent stops
            // at the first float that reaches `code`, whose predecessor is then known to fall short.
            const auto encoded = (static_cast<float>(code) - 0.5F) / kCodeMaximum;
            auto threshold = Transfer::srgbToLinear(encoded);
            if (exactQuantize(threshold) < code) {
                do {
                    threshold = std::nextafter(threshold, 1.0F);
                } while (exactQuantize(threshold) < code);
                return threshold;
            }
            // The descent walks the same float sequence as the former for loop; a while loop
            // because a floating-point loop counter is what bugprone-float-loop-counter forbids.
            auto previous = std::nextafter(threshold, 0.0F);
            while (exactQuantize(previous) >= code) {
                threshold = previous;
                previous = std::nextafter(previous, 0.0F);
            }
            return threshold;
        }

    } // namespace

    SrgbOutputTables::SrgbOutputTables()
    {
        constexpr std::size_t kThresholdCount = static_cast<std::size_t>(kCodeMaximum);
        for (std::size_t index = 0; index < kThresholdCount; ++index) {
            thresholds_[index] = outputThreshold(static_cast<std::uint16_t>(index + 1));
        }
        for (std::size_t index = kThresholdCount; index < thresholds_.size(); ++index) {
            thresholds_[index] = std::numeric_limits<float>::infinity();
        }
        // buckets_[b] is the number of thresholds at or below b / kBucketCount, i.e. the index
        // upper_bound would return for that boundary: one merge pass over the ascending thresholds.
        std::size_t count = 0;
        for (std::size_t bucket = 0; bucket < buckets_.size(); ++bucket) {
            const auto boundary = static_cast<float>(bucket) / static_cast<float>(kBucketCount);
            while (count < kThresholdCount && thresholds_[count] <= boundary) {
                ++count;
            }
            buckets_[bucket] = static_cast<std::uint16_t>(count);
        }
    }

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

    const SrgbOutputTables &ColourTables::output() const noexcept
    {
        return output_;
    }

    std::span<const float> ColourTables::transfer(const std::uint16_t characteristics) const
    {
        const std::lock_guard lock{mutex_};
        auto &entry = transfers_[transferSlot(characteristics)];
        if (!entry) {
            auto values = std::make_unique<Table>();
            for (std::size_t code = 0; code < values->size(); ++code) {
                (*values)[code] = Transfer::toLinear(characteristics, static_cast<float>(code) / kCodeMaximum);
            }
            entry = std::move(values);
        }
        return *entry;
    }

    std::span<const float> ColourTables::gains(const float peakNits, const std::span<const float> transfer) const
    {
        const std::lock_guard lock{mutex_};
        for (const auto &entry : gains_) {
            if (entry.values && entry.peakNits == peakNits) {
                return *entry.values;
            }
        }
        for (auto &entry : gains_) {
            if (!entry.values) {
                auto values = makeGainTable(peakNits, transfer);
                entry.peakNits = peakNits;
                entry.values = std::move(values);
                return *entry.values;
            }
        }
        return {};
    }

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits,
                               const ColourTables &tables)
        : Presentation(cicp, masteringPeakNits, std::nullopt, tables)
    {
    }

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits,
                               const std::optional<Primaries::Chromaticities> &chromaticities,
                               const ColourTables &tables)
        : Presentation(cicp, masteringPeakNits,
                       chromaticities && Primaries::isUsable(*chromaticities) ? chromaticities : std::nullopt, tables,
                       Usable{})
    {
    }

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits,
                               const std::optional<Primaries::Chromaticities> &chromaticities,
                               const ColourTables &tables, Usable)
        : active_(needed(cicp, chromaticities)), hlg_(cicp.transfer == 18),
          toneMapAfterMatrix_(Transfer::isHdr(cicp.transfer) && (hlg_ || chromaticities.has_value())),
          sourcePeakNits_(colour::sourcePeakNits(cicp.transfer, masteringPeakNits)),
          primaries_(chromaticities ? Primaries::toSrgb(*chromaticities) : Primaries::toSrgb(cicp.primaries)),
          sourceLuminance_(chromaticities ? Primaries::luminanceCoefficients(*chromaticities)
                                          : Primaries::luminanceCoefficients(cicp.primaries)),
          toneMap_(sourcePeakNits_), outputTables_(tables.output())
    {
        if (!active_) {
            return;
        }
        transferLut_ = tables.transfer(cicp.transfer);
        if (Transfer::isHdr(cicp.transfer) && !toneMapAfterMatrix_) {
            toneGain_ = tables.gains(sourcePeakNits_, transferLut_);
            if (toneGain_.empty()) {
                ownedToneGain_ = makeGainTable(sourcePeakNits_, transferLut_);
                toneGain_ = *ownedToneGain_;
            }
        }
    }

    bool Presentation::needed(const Cicp &cicp) noexcept
    {
        const bool transferConversion =
            cicp.transfer == 4 || cicp.transfer == 5 || cicp.transfer == 8 || Transfer::isHdr(cicp.transfer);
        return !Primaries::isIdentity(cicp.primaries) || transferConversion;
    }

    bool Presentation::needed(const Cicp &cicp, const std::optional<Primaries::Chromaticities> &chromaticities) noexcept
    {
        return (chromaticities && Primaries::isUsable(*chromaticities)) || needed(cicp);
    }

    template <class Load, class Store>
    void Presentation::convertPixels(const std::size_t pixelCount, const Load &load, const Store &store,
                                     const bool rgbOrder) const noexcept
    {
        const std::size_t redChannel = rgbOrder ? 0U : 2U;
        const std::size_t blueChannel = 2U - redChannel;
        constexpr std::size_t kBlock = 64;
        std::array<float, kBlock> red{};
        std::array<float, kBlock> green{};
        std::array<float, kBlock> blue{};
        for (std::size_t base = 0; base < pixelCount; base += kBlock) {
            const auto count = std::min(kBlock, pixelCount - base);

            // Pass 1: the transfer LUT, the HLG OOTF and, on the table path, the EETF gain.
            for (std::size_t index = 0; index < count; ++index) {
                const auto pixel = base + index;
                const auto redCode = load(pixel, redChannel);
                const auto greenCode = load(pixel, 1);
                const auto blueCode = load(pixel, blueChannel);
                Primaries::Rgb linear{transferLut_[redCode], transferLut_[greenCode], transferLut_[blueCode]};
                if (hlg_) {
                    linear = hlgDisplayNits(linear, sourceLuminance_);
                }
                if (!toneGain_.empty()) {
                    // The LUT is monotone non-decreasing, so the largest linear value is the LUT
                    // at the largest code and the table holds its EETF gain: bit for bit what
                    // applyMaxRgb computes per pixel (tests/support/PresentationReference.hpp,
                    // PipelineTests). A maximum code of 0 is the no-light case, which the curve
                    // maps to the display's black.
                    const auto maximumCode = std::max({redCode, greenCode, blueCode});
                    const auto gain = toneGain_[maximumCode];
                    linear = maximumCode == 0 ? Primaries::Rgb{kTargetBlack, kTargetBlack, kTargetBlack}
                                              : Primaries::Rgb{linear[0] * gain, linear[1] * gain, linear[2] * gain};
                }
                red[index] = linear[0];
                green[index] = linear[1];
                blue[index] = linear[2];
            }

            // Pass 2: the primaries matrix, one pixel per lane.
            for (std::size_t index = 0; index < count; ++index) {
                const auto converted = Primaries::apply(primaries_, {red[index], green[index], blue[index]});
                red[index] = converted[0];
                green[index] = converted[1];
                blue[index] = converted[2];
            }

            // Pass 3: the scalar path's EETF after the matrix, then the exact quantiser.
            for (std::size_t index = 0; index < count; ++index) {
                Primaries::Rgb linear{red[index], green[index], blue[index]};
                if (toneMapAfterMatrix_) {
                    linear = ToneMap::applyMaxRgb(toneMap_, linear);
                }
                const auto pixel = base + index;
                store(pixel, blueChannel, outputTables_.quantize(linear[2]));
                store(pixel, 1, outputTables_.quantize(linear[1]));
                store(pixel, redChannel, outputTables_.quantize(linear[0]));
            }
        }
    }

    void Presentation::apply(const std::span<std::uint16_t> bgraRow, const bool rgbOrder,
                             const unsigned channels) const noexcept
    {
        if (!active_) {
            return;
        }
        convertPixels(
            bgraRow.size() / channels,
            [bgraRow, channels](const std::size_t pixel, const std::size_t channel) {
                return bgraRow[pixel * channels + channel];
            },
            [bgraRow, channels](const std::size_t pixel, const std::size_t channel, const std::uint16_t code) {
                bgraRow[pixel * channels + channel] = code;
            },
            rgbOrder);
    }

    void Presentation::apply(const std::span<std::byte> bgraRow, const bool rgbOrder,
                             const unsigned channels) const noexcept
    {
        if (!active_) {
            return;
        }
        // Little-endian 16-bit samples in byte storage (PixelBuffer owns bytes, not samples).
        const auto bytesPerPixel = channels * 2U;
        convertPixels(
            bgraRow.size() / bytesPerPixel,
            [bgraRow, bytesPerPixel](const std::size_t pixel, const std::size_t channel) {
                const auto offset = pixel * bytesPerPixel + channel * 2;
                return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bgraRow[offset]) |
                                                  (std::to_integer<std::uint8_t>(bgraRow[offset + 1]) << 8U));
            },
            [bgraRow, bytesPerPixel](const std::size_t pixel, const std::size_t channel, const std::uint16_t code) {
                const auto offset = pixel * bytesPerPixel + channel * 2;
                bgraRow[offset] = std::byte{static_cast<std::uint8_t>(code & 0xffU)};
                bgraRow[offset + 1] = std::byte{static_cast<std::uint8_t>(code >> 8U)};
            },
            rgbOrder);
    }

    void Presentation::applyImage(const std::span<std::byte> pixels, const std::uint32_t pitchBytes,
                                  const std::uint32_t height, const unsigned maxThreads, const bool rgbOrder,
                                  const unsigned channels) const
    {
        constexpr std::uint64_t kParallelThresholdPixels = 256ULL * 1'024ULL;
        constexpr unsigned kMaximumBands = 4;
        const auto pixelCount = static_cast<std::uint64_t>(pixels.size()) / (static_cast<std::uint64_t>(channels) * 2U);
        const auto bandCount = std::min(std::clamp(maxThreads, 1U, kMaximumBands), height);
        if (pixelCount < kParallelThresholdPixels || bandCount == 1) {
            apply(pixels, rgbOrder, channels);
            return;
        }

        // This object is immutable after construction and apply() is const noexcept, so the
        // workers share it safely; every band is a disjoint row range. The last band runs on the
        // calling thread, and the BandWorkers destructor joins the others before this returns,
        // on the normal path and while unwinding.
        BandWorkers workers{bandCount - 1};
        const auto rowsPerBand = height / bandCount;
        const auto extraRows = height % bandCount;
        std::uint32_t firstRow = 0;
        for (unsigned bandIndex = 0; bandIndex + 1 < bandCount; ++bandIndex) {
            const auto rowCount = rowsPerBand + (bandIndex < extraRows ? 1U : 0U);
            const auto band = pixels.subspan(static_cast<std::size_t>(firstRow) * pitchBytes,
                                             static_cast<std::size_t>(rowCount) * pitchBytes);
            workers.start([this, band, rgbOrder, channels]() noexcept { apply(band, rgbOrder, channels); });
            firstRow += rowCount;
        }
        apply(pixels.subspan(static_cast<std::size_t>(firstRow) * pitchBytes), rgbOrder, channels);
    }

    float Presentation::sourcePeakNits() const noexcept
    {
        return sourcePeakNits_;
    }

    const SrgbOutputTables &Presentation::outputTables() const noexcept
    {
        return outputTables_;
    }

    std::span<const float> Presentation::toneMapGains() const noexcept
    {
        return toneGain_;
    }

    std::span<const float> Presentation::transferValues() const noexcept
    {
        return transferLut_;
    }

} // namespace pvdkit::core::colour
