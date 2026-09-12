#include "adapters/win/FileSource.hpp"

#include <string>
#include <utility>

#include "adapters/win/FileMapping.hpp"
#include "adapters/win/Utf8.hpp"

namespace pvdkit::win {

core::Result<std::unique_ptr<core::IFileData>> FileSource::open(
    const std::string_view utf8Path) {
  return utf8ToWide(utf8Path)
      .and_then([](const std::wstring& wide) { return toWin32Path(wide); })
      .and_then([](const std::wstring& path) { return FileMapping::open(path); })
      // By reference on purpose: for i686-pc-windows-msvc, clang 19 splits a captureless lambda
      // that takes a non-trivially-copyable parameter by value into an `__impl` body plus a
      // thunk and then names the coverage record differently from the profile name, which makes
      // llvm-cov refuse the whole binary ("function name is empty"). An rvalue reference keeps
      // one function and the coverage-x86 gate readable; the call is a move either way.
      .transform([](std::unique_ptr<FileMapping>&& mapping) {
        return std::unique_ptr<core::IFileData>{std::move(mapping)};
      });
}

}  // namespace pvdkit::win
