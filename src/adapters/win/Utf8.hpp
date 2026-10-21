#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "core/Error.hpp"

namespace pvdkit::win
{

    // Internal helpers exposed for unit tests; not part of the adapter contract.
    namespace detail
    {

        /// `MultiByteToWideChar` measures its input with an `int`; longer inputs are `FileOpenFailed`.
        [[nodiscard]] core::Result<int> utf8Length(std::size_t size);

        /// `WideCharToMultiByte` measures its input with an `int`; longer inputs cannot be converted.
        [[nodiscard]] core::Result<int> wideLength(std::size_t size);

    } // namespace detail

    [[nodiscard]] core::Result<std::wstring> utf8ToWide(std::string_view utf8);

    /// Converts a UTF-16 boundary string to UTF-8. Unpaired surrogates and inputs longer than
    /// `INT_MAX` code units are `ConversionFailed`; Win32 never throws.
    [[nodiscard]] core::Result<std::string> wideToUtf8(std::wstring_view wide);

    /// Returns the path to hand to `CreateFileW`. Paths shorter than `MAX_PATH` are returned
    /// unchanged so Win32 keeps normalising `/`, `.`, `..` and trailing dots/spaces; at or above
    /// `MAX_PATH` the path is normalised with `GetFullPathNameW` (which also resolves a relative
    /// path against the current directory) and prefixed with `\\?\` (`\\?\UNC\server\share\...` for
    /// UNC paths). Paths already starting with `\\?\` or `\\.\` are never touched. Fails with
    /// `FileOpenFailed` when Win32 cannot normalise the path (e.g. beyond the 32767-character limit).
    [[nodiscard]] core::Result<std::wstring> toWin32Path(std::wstring_view path);

    /// Returns a stable absolute native path for opening and for IFileData::sourcePath: relative
    /// and drive-relative names are resolved against the current directory, separators and
    /// `.`/`..`/trailing dots and spaces are normalised as Win32 would, paths at or above
    /// `MAX_PATH` receive the extended prefix, and existing `\\?\`/`\\.\` names pass through.
    [[nodiscard]] core::Result<std::wstring> toAbsoluteWin32Path(std::wstring_view path);

} // namespace pvdkit::win
