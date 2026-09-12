#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace pvdkit::sequence
{

    enum class FixtureDisposition : std::uint8_t
    {
        Accepted,
        Rejected,
        DecodeFailure,
    };

    // Defined by the plugin-local SequenceExpectations.cpp compiled into one sequence executable.
    [[nodiscard]] std::optional<FixtureDisposition> fixtureDisposition(std::string_view relativePath);

} // namespace pvdkit::sequence
