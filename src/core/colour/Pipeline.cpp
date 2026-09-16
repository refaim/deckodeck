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
        for (std::size_t index = 0; index < thresholds_.size(); ++index) {
            thresholds_[index] = outputThreshold(static_cast<std::uint16_t>(index + 1));
        }
        // buckets_[b] is the number of thresholds at or below b / kBucketCount, i.e. the index
        // upper_bound returns for that boundary: one merge pass over the ascending thresholds.
        std::size_t count = 0;
        for (std::size_t bucket = 0; bucket < buckets_.size(); ++bucket) {
            const auto boundary = static_cast<float>(bucket) / static_cast<float>(kBucketCount);
            while (count < thresholds_.size() && thresholds_[count] <= boundary) {
                ++count;
            }
            buckets_[bucket] = static_cast<std::uint16_t>(count);
        }
    }

    std::uint16_t SrgbOutputTables::quantize(const float linear) const noexcept
    {
        // The negated comparison also sends NaN to code 0, the exact path's answer on this CRT.
        if (!(linear > 0.0F)) {
            return 0;
        }
        if (linear >= 1.0F) {
            return kCodeMaximum;
        }

        const auto bucket = static_cast<std::size_t>(linear * static_cast<float>(kBucketCount));
        const auto first = static_cast<std::ptrdiff_t>(buckets_[bucket]);
        const auto last = static_cast<std::ptrdiff_t>(buckets_[bucket + 1]);
        // The bucket only narrows the exact threshold search; no OETF value is interpolated.
        const auto found = std::upper_bound(thresholds_.begin() + first, thresholds_.begin() + last, linear);
        return static_cast<std::uint16_t>(found - thresholds_.begin());
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

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits,
                               const SrgbOutputTables &outputTables)
        : Presentation(cicp, masteringPeakNits, std::nullopt, outputTables)
    {
    }

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits,
                               const std::optional<Primaries::Chromaticities> &chromaticities,
                               const SrgbOutputTables &outputTables)
        : Presentation(cicp, masteringPeakNits,
                       chromaticities && Primaries::isUsable(*chromaticities) ? chromaticities : std::nullopt,
                       outputTables, Usable{})
    {
    }

    Presentation::Presentation(const Cicp &cicp, const std::optional<float> masteringPeakNits,
                               const std::optional<Primaries::Chromaticities> &chromaticities,
                               const SrgbOutputTables &outputTables, Usable)
        : active_(needed(cicp, chromaticities)), hlg_(cicp.transfer == 18), hdr_(Transfer::isHdr(cicp.transfer)),
          sourcePeakNits_(colour::sourcePeakNits(cicp.transfer, masteringPeakNits)),
          primaries_(chromaticities ? Primaries::toSrgb(*chromaticities) : Primaries::toSrgb(cicp.primaries)),
          sourceLuminance_(chromaticities ? Primaries::luminanceCoefficients(*chromaticities)
                                          : Primaries::luminanceCoefficients(cicp.primaries)),
          toneMap_(sourcePeakNits_), outputTables_(outputTables)
    {
        if (!active_) {
            return;
        }
        transferLut_ = std::make_unique<TransferLut>();
        for (std::size_t sample = 0; sample < transferLut_->size(); ++sample) {
            const auto encoded = static_cast<float>(sample) / kCodeMaximum;
            (*transferLut_)[sample] = Transfer::toLinear(cicp.transfer, encoded);
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
        return chromaticities.has_value() || needed(cicp);
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

    void Presentation::applyImage(const std::span<std::byte> pixels, const std::uint32_t pitchBytes,
                                  const std::uint32_t height, const unsigned maxThreads) const
    {
        constexpr std::uint64_t kParallelThresholdPixels = 256ULL * 1'024ULL;
        constexpr unsigned kMaximumBands = 4;
        const auto pixelCount = static_cast<std::uint64_t>(pixels.size()) / 8;
        const auto bandCount = std::min(std::clamp(maxThreads, 1U, kMaximumBands), height);
        if (pixelCount < kParallelThresholdPixels || bandCount == 1) {
            apply(pixels);
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
            workers.start([this, band]() noexcept { apply(band); });
            firstRow += rowCount;
        }
        apply(pixels.subspan(static_cast<std::size_t>(firstRow) * pitchBytes));
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
        return {outputTables_.quantize(linear[2]), outputTables_.quantize(linear[1]),
                outputTables_.quantize(linear[0])};
    }

    float Presentation::sourcePeakNits() const noexcept
    {
        return sourcePeakNits_;
    }

    const SrgbOutputTables &Presentation::outputTables() const noexcept
    {
        return outputTables_;
    }

} // namespace pvdkit::core::colour
