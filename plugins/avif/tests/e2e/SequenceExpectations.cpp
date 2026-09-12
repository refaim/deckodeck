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
            std::ranges::find(avif::tests::kAccepted, relativePath, &avif::tests::FixtureExpectation::name);
        if (accepted != avif::tests::kAccepted.end()) {
            return FixtureDisposition::Accepted;
        }
        if (std::ranges::find(avif::tests::kUnsupported, relativePath) != avif::tests::kUnsupported.end() ||
            std::ranges::find(avif::tests::kRejected, relativePath) != avif::tests::kRejected.end()) {
            return FixtureDisposition::Rejected;
        }
        if (std::ranges::find(avif::tests::kDecodeFailures, relativePath) != avif::tests::kDecodeFailures.end()) {
            return FixtureDisposition::DecodeFailure;
        }
        return std::nullopt;
    }

} // namespace pvdkit::sequence
