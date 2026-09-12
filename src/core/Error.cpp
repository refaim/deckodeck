#include "core/Error.hpp"

namespace avifpvd::core {

std::string_view name(const ErrorCode code) {
  switch (code) {
    case ErrorCode::NotAvif:
      return "NotAvif";
    case ErrorCode::FileOpenFailed:
      return "FileOpenFailed";
    case ErrorCode::ParseFailed:
      return "ParseFailed";
    case ErrorCode::DecodeFailed:
      return "DecodeFailed";
    case ErrorCode::ConversionFailed:
      return "ConversionFailed";
    case ErrorCode::PageOutOfRange:
      return "PageOutOfRange";
    case ErrorCode::Aborted:
      return "Aborted";
    case ErrorCode::TooLarge:
      return "TooLarge";
    case ErrorCode::UnsupportedFeature:
      return "UnsupportedFeature";
    case ErrorCode::InvalidTransform:
      return "InvalidTransform";
    case ErrorCode::Internal:
      return "Internal";
  }
  return "Unknown";
}

}  // namespace avifpvd::core
