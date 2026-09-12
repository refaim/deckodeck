#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "core/Error.hpp"

namespace avifpvd::core {

/// Provides a read-only view of owned file bytes.
class IFileData {
 public:
  virtual ~IFileData() = default;
  [[nodiscard]] virtual std::span<const std::byte> bytes() const = 0;
};

/// Opens UTF-8 paths as owned read-only file data.
class IFileSource {
 public:
  virtual ~IFileSource() = default;
  [[nodiscard]] virtual Result<std::unique_ptr<IFileData>> open(std::string_view utf8Path) = 0;
};

}  // namespace avifpvd::core
