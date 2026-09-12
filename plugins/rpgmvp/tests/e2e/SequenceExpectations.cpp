#include <algorithm>
#include <optional>
#include <string_view>

#include "FixtureExpectations.hpp"
#include "sequence/SequenceExpectations.hpp"

namespace pvdkit::sequence
{

    std::optional<FixtureDisposition> fixtureDisposition(const std::string_view relativePath)
    {
        const auto accepted =
            std::ranges::find(rpgmvp::tests::kAccepted, relativePath, &rpgmvp::tests::FixtureExpectation::name);
        if (accepted != rpgmvp::tests::kAccepted.end()) {
            return FixtureDisposition::Accepted;
        }
        if (std::ranges::find(rpgmvp::tests::kRejected, relativePath) != rpgmvp::tests::kRejected.end()) {
            return FixtureDisposition::Rejected;
        }
        if (std::ranges::find(rpgmvp::tests::kDecodeFailures, relativePath) != rpgmvp::tests::kDecodeFailures.end()) {
            return FixtureDisposition::DecodeFailure;
        }
        return std::nullopt;
    }

} // namespace pvdkit::sequence
