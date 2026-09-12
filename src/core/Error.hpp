#pragma once

#include <expected>
#include <string>
#include <string_view>

static_assert(__cplusplus >= 202302L, "AVIF.pvd requires C++23");

namespace avifpvd::core {

/// Identifies an expected AVIF decoding failure.
enum class ErrorCode {
  NotAvif,
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
struct Error {
  ErrorCode code;
  std::string detail;
};

/// Represents either a value or an expected decoding error.
template <class T>
using Result = std::expected<T, Error>;

/// Returns the stable diagnostic name of an error category.
std::string_view name(ErrorCode code);

}  // namespace avifpvd::core
