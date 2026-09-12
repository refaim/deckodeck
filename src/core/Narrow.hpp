#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "core/Error.hpp"

namespace pvdkit::core
{

    /// Narrows a 64-bit count to `To`, refusing a value `To` cannot hold as `TooLarge`; `what` names
    /// the quantity in the error detail. Every byte count in this plugin (pixel buffer sizes, file
    /// sizes) is computed in `std::uint64_t` and passes through here before it becomes a
    /// `std::size_t`, so on the 32-bit build (where `std::size_t` counts 4 GiB) nothing wraps and a
    /// file or picture beyond the address space is refused instead of truncated.
    template <std::unsigned_integral To>
    [[nodiscard]] Result<To> narrow(const std::uint64_t value, const std::string_view what)
    {
        if (!std::in_range<To>(value)) {
            return std::unexpected(
                Error{ErrorCode::TooLarge, std::string{what} + " (" + std::to_string(value) + ") does not fit in " +
                                               std::to_string(std::numeric_limits<To>::digits) + " bits"});
        }
        return static_cast<To>(value);
    }

} // namespace pvdkit::core
