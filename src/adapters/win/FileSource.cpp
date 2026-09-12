#include "adapters/win/FileSource.hpp"

#include <string>
#include <utility>

#include "adapters/win/FileMapping.hpp"
#include "adapters/win/Utf8.hpp"

namespace avifpvd::win {

core::Result<std::unique_ptr<core::IFileData>> FileSource::open(
    const std::string_view utf8Path) {
  return utf8ToWide(utf8Path)
      .and_then([](const std::wstring& wide) { return toWin32Path(wide); })
      .and_then([](const std::wstring& path) { return FileMapping::open(path); })
      .transform([](std::unique_ptr<FileMapping> mapping) {
        return std::unique_ptr<core::IFileData>{std::move(mapping)};
      });
}

}  // namespace avifpvd::win
