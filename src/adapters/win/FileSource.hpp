#pragma once

#include <memory>
#include <string_view>

#include "core/IFileSource.hpp"

namespace avifpvd::win {

class FileSource final : public core::IFileSource {
 public:
  [[nodiscard]] core::Result<std::unique_ptr<core::IFileData>> open(
      std::string_view utf8Path) override;
};

}  // namespace avifpvd::win
