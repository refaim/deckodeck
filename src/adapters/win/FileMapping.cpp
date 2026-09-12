#include "adapters/win/FileMapping.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>

namespace avifpvd::win {
namespace {

core::Error fileError(const std::string_view operation, const DWORD error) {
  return {core::ErrorCode::FileOpenFailed,
          std::string{operation} + " (Win32 error " + std::to_string(error) + ")"};
}

}  // namespace

void CloseHandleDestroy::operator()(pointer handle) const noexcept { static_cast<void>(CloseHandle(handle)); }

void UnmapViewDestroy::operator()(std::byte* view) const noexcept { static_cast<void>(UnmapViewOfFile(view)); }

core::Result<std::size_t> detail::fileSize(const UniqueHandle& file) {
  LARGE_INTEGER size{};
  if (GetFileSizeEx(file.get(), &size) == FALSE) {
    return std::unexpected(fileError("GetFileSizeEx failed", GetLastError()));
  }
  if (size.QuadPart == 0) {
    // Not a Win32 failure: a zero-length file simply cannot be mapped (CreateFileMappingW
    // rejects an empty range), so the detail carries no invented error code.
    return std::unexpected(core::Error{core::ErrorCode::FileOpenFailed, "file is empty"});
  }
  return static_cast<std::size_t>(size.QuadPart);
}

core::Result<UniqueHandle> detail::createReadOnlyMapping(const UniqueHandle& file) {
  UniqueHandle mapping{CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};
  if (!mapping) {
    return std::unexpected(fileError("CreateFileMappingW failed", GetLastError()));
  }
  return mapping;
}

core::Result<UniqueView> detail::mapReadOnly(const UniqueHandle& mapping) {
  UniqueView view{
      static_cast<std::byte*>(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0))};
  if (!view) {
    return std::unexpected(fileError("MapViewOfFile failed", GetLastError()));
  }
  return view;
}

FileMapping::FileMapping(State state) noexcept
    : file_(std::move(state.file)),
      mapping_(std::move(state.mapping)),
      view_(std::move(state.view)),
      size_(state.size) {}

core::Result<std::unique_ptr<FileMapping>> FileMapping::open(const std::wstring_view path) {
  const std::wstring nullTerminatedPath{path};
  UniqueHandle file{CreateFileW(nullTerminatedPath.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
  if (file.get() == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    static_cast<void>(file.release());
    return std::unexpected(fileError("CreateFileW failed", error));
  }

  return detail::fileSize(file).and_then([&](const std::size_t size) {
    return detail::createReadOnlyMapping(file).and_then([&](UniqueHandle mapping) {
      return detail::mapReadOnly(mapping).transform([&](UniqueView view) {
        State state{std::move(file), std::move(mapping), std::move(view), size};
        return std::make_unique<FileMapping>(std::move(state));
      });
    });
  });
}

std::span<const std::byte> FileMapping::bytes() const { return {view_.get(), size_}; }

}  // namespace avifpvd::win
