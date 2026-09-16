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
            std::ranges::find(exr::tests::kAccepted, relativePath, &exr::tests::FixtureExpectation::name);
        if (accepted != exr::tests::kAccepted.end()) {
            return FixtureDisposition::Accepted;
        }
        if (std::ranges::find(exr::tests::kRejected, relativePath) != exr::tests::kRejected.end()) {
            return FixtureDisposition::Rejected;
        }
        if (std::ranges::find(exr::tests::kDecodeFailures, relativePath) != exr::tests::kDecodeFailures.end()) {
            return FixtureDisposition::DecodeFailure;
        }
        return std::nullopt;
    }

} // namespace pvdkit::sequence
