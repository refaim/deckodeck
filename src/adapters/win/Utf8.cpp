#include "adapters/win/Utf8.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <limits>
#include <string>
#include <utility>

namespace avifpvd::win {
namespace {

constexpr std::wstring_view kExtendedPrefix = LR"(\\?\)";
constexpr std::wstring_view kDevicePrefix = LR"(\\.\)";
constexpr std::wstring_view kUncPrefix = LR"(\\)";
constexpr std::wstring_view kExtendedUncPrefix = LR"(\\?\UNC\)";

core::Error win32Error(const std::string_view operation, const DWORD error) {
  return {core::ErrorCode::FileOpenFailed,
          std::string{operation} + " (Win32 error " + std::to_string(error) + ")"};
}

// Absolute, backslash-separated form of `path` with `.`/`..` segments and trailing dots/spaces
// resolved exactly as Win32 would resolve them for a short path.
core::Result<std::wstring> fullPathName(const std::wstring_view path) {
  const std::wstring input{path};
  const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
  if (required == 0) {
    return std::unexpected(win32Error("GetFullPathNameW rejected the path", GetLastError()));
  }
  // The first call reports a buffer size that is only an upper bound (it counts the input as
  // written, before `..` segments collapse); the second call returns the exact length.
  std::wstring full(static_cast<std::size_t>(required), L'\0');
  full.resize(GetFullPathNameW(input.c_str(), required, full.data(), nullptr));
  return full;
}

// GetFullPathNameW canonicalises the forward-slash spellings `//?/` and `//./` into the `\\?\`
// and `\\.\` prefixes itself; such a result already names the NT or device namespace and must be
// used as is (treating it as UNC would produce `\\?\UNC\?\...`).
std::wstring extended(std::wstring full) {
  if (full.starts_with(kExtendedPrefix) || full.starts_with(kDevicePrefix)) {
    return full;
  }
  if (full.starts_with(kUncPrefix)) {
    return std::wstring{kExtendedUncPrefix} + full.substr(kUncPrefix.size());
  }
  return std::wstring{kExtendedPrefix} + std::move(full);
}

}  // namespace

core::Result<int> detail::utf8Length(const std::size_t size) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::unexpected(
        core::Error{core::ErrorCode::FileOpenFailed, "the UTF-8 path is longer than INT_MAX bytes"});
  }
  return static_cast<int>(size);
}

core::Result<std::wstring> utf8ToWide(const std::string_view utf8) {
  if (utf8.empty()) {
    return std::wstring{};
  }

  return detail::utf8Length(utf8.size()).and_then([&](const int utf8Size) -> core::Result<std::wstring> {
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), utf8Size,
                                             nullptr, 0);
    if (required == 0) {
      return std::unexpected(win32Error("MultiByteToWideChar rejected the UTF-8 path", GetLastError()));
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    static_cast<void>(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), utf8Size,
                                          wide.data(), required));
    return wide;
  });
}

core::Result<std::wstring> toWin32Path(const std::wstring_view path) {
  if (path.size() < MAX_PATH || path.starts_with(kExtendedPrefix) ||
      path.starts_with(kDevicePrefix)) {
    return std::wstring{path};
  }
  return fullPathName(path).transform(extended);
}

}  // namespace avifpvd::win
