#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

#include "FixtureExpectations.hpp"
#include "sequence/SequenceExpectations.hpp"

namespace pvdkit::sequence
{
    namespace
    {

        constexpr std::array<std::string_view, 5> kAcceptedWebp{
            "webp_rgb_lossless_2x2.png_", "webp_rgba_lossless_2x2.png_", "webp_rgb_lossy_4x4.png_",
            "webp_rgba_lossy_4x4.png_", "webp_metadata_2x2.png_"};

    } // namespace

    std::optional<FixtureDisposition> fixtureDisposition(const std::string_view relativePath)
    {
        const auto accepted =
            std::ranges::find(rpgmvp::tests::kAccepted, relativePath, &rpgmvp::tests::FixtureExpectation::name);
        if (accepted != rpgmvp::tests::kAccepted.end() ||
            std::ranges::find(kAcceptedWebp, relativePath) != kAcceptedWebp.end()) {
            return FixtureDisposition::Accepted;
        }
        if (std::ranges::find(rpgmvp::tests::kRejected, relativePath) != rpgmvp::tests::kRejected.end() ||
            relativePath == "webp_animation_2x2.png_") {
            return FixtureDisposition::Rejected;
        }
        if (std::ranges::find(rpgmvp::tests::kDecodeFailures, relativePath) != rpgmvp::tests::kDecodeFailures.end()) {
            return FixtureDisposition::DecodeFailure;
        }
        return std::nullopt;
    }

} // namespace pvdkit::sequence
