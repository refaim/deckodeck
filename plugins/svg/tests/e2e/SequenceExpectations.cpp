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
            std::ranges::find(svg::tests::kAccepted, relativePath, &svg::tests::FixtureExpectation::name);
        if (accepted != svg::tests::kAccepted.end()) {
            return FixtureDisposition::Accepted;
        }
        const auto supportAccepted =
            std::ranges::find(svg::tests::kSupportAccepted, relativePath, &svg::tests::FixtureExpectation::name);
        if (supportAccepted != svg::tests::kSupportAccepted.end()) {
            return FixtureDisposition::Accepted;
        }
        if (std::ranges::find(svg::tests::kRejected, relativePath) != svg::tests::kRejected.end()) {
            return FixtureDisposition::Rejected;
        }
        return std::nullopt;
    }

} // namespace pvdkit::sequence
