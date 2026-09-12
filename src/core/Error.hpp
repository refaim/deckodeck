#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

static_assert(__cplusplus >= 202302L, "pvdkit requires C++23");

namespace pvdkit::core
{

    /// Identifies an expected decoding failure; NotRecognised makes the host try the next decoder.
    enum class ErrorCode : std::uint8_t
    {
        NotRecognised,
        FileOpenFailed,
        ParseFailed,
        DecodeFailed,
        ConversionFailed,
        PageOutOfRange,
        Aborted,
        TooLarge,
        UnsupportedFeature,
        InvalidTransform,
        Internal,
    };

    /// Carries a stable error category and diagnostic detail.
    struct Error
    {
        ErrorCode code = ErrorCode::Internal;
        std::string detail;
    };

    /// Represents either a value or an expected decoding error.
    template <class T> using Result = std::expected<T, Error>;

    /// Returns the stable diagnostic name of an error category.
    std::string_view name(ErrorCode code);

} // namespace pvdkit::core
