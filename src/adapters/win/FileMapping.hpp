#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "core/IFileSource.hpp"

namespace pvdkit::win {

struct CloseHandleDestroy {
  using pointer = void*;
  void operator()(pointer handle) const noexcept;
};

struct UnmapViewDestroy {
  void operator()(std::byte* view) const noexcept;
};

using UniqueHandle = std::unique_ptr<void, CloseHandleDestroy>;
using UniqueView = std::unique_ptr<std::byte, UnmapViewDestroy>;

// Internal helpers exposed for unit tests; not part of the adapter contract.
namespace detail {

[[nodiscard]] core::Result<std::size_t> fileSize(const UniqueHandle& file);
[[nodiscard]] core::Result<UniqueHandle> createReadOnlyMapping(const UniqueHandle& file);
[[nodiscard]] core::Result<UniqueView> mapReadOnly(const UniqueHandle& mapping);

}  // namespace detail

class FileMapping final : public core::IFileData {
 public:
  struct State {
    UniqueHandle file;
    UniqueHandle mapping;
    UniqueView view;
    std::size_t size;
  };

  explicit FileMapping(State state) noexcept;
  ~FileMapping() override = default;

  FileMapping(const FileMapping&) = delete;
  FileMapping& operator=(const FileMapping&) = delete;
  FileMapping(FileMapping&&) = delete;
  FileMapping& operator=(FileMapping&&) = delete;

  [[nodiscard]] static core::Result<std::unique_ptr<FileMapping>> open(std::wstring_view path);
  [[nodiscard]] std::span<const std::byte> bytes() const override;

 private:
  UniqueHandle file_;
  UniqueHandle mapping_;
  UniqueView view_;
  std::size_t size_;
};

}  // namespace pvdkit::win
