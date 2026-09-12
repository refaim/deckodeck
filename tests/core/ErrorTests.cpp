#include <array>
#include <ostream>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

#include "core/Error.hpp"

namespace pvdkit::core {
namespace {

TEST_CASE("every error code has a stable diagnostic name") {
  constexpr std::array expected{
      std::pair{ErrorCode::NotRecognised, std::string_view{"NotRecognised"}},
      std::pair{ErrorCode::FileOpenFailed, std::string_view{"FileOpenFailed"}},
      std::pair{ErrorCode::ParseFailed, std::string_view{"ParseFailed"}},
      std::pair{ErrorCode::DecodeFailed, std::string_view{"DecodeFailed"}},
      std::pair{ErrorCode::ConversionFailed, std::string_view{"ConversionFailed"}},
      std::pair{ErrorCode::PageOutOfRange, std::string_view{"PageOutOfRange"}},
      std::pair{ErrorCode::Aborted, std::string_view{"Aborted"}},
      std::pair{ErrorCode::TooLarge, std::string_view{"TooLarge"}},
      std::pair{ErrorCode::UnsupportedFeature, std::string_view{"UnsupportedFeature"}},
      std::pair{ErrorCode::InvalidTransform, std::string_view{"InvalidTransform"}},
      std::pair{ErrorCode::Internal, std::string_view{"Internal"}},
  };

  for (const auto& [code, expectedName] : expected) {
    CHECK(name(code) == expectedName);
  }
}

TEST_CASE("an unknown error value has a diagnostic name") {
  CHECK(name(static_cast<ErrorCode>(255)) == "Unknown");
}

}  // namespace
}  // namespace pvdkit::core
