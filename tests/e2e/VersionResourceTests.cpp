// Reads the VERSIONINFO resource back from the built plugin through the Win32 version API
// (version.lib is linked into this test only; the plugin keeps importing KERNEL32.dll alone).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "PluginHost.hpp"
#include "pvd/PluginConstants.hpp"

namespace
{

    std::wstring utf8ToWide(const std::string_view utf8)
    {
        const int length =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        REQUIRE(length > 0);
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        static_cast<void>(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                                              wide.data(), length));
        return wide;
    }

    class VersionBlock
    {
      public:
        explicit VersionBlock(const std::filesystem::path &module)
        {
            DWORD ignored = 0;
            const DWORD size = GetFileVersionInfoSizeW(module.c_str(), &ignored);
            const DWORD sizeError = size == 0 ? GetLastError() : 0;
            CAPTURE(sizeError);
            REQUIRE(size != 0);
            block_.resize(size);
            REQUIRE(GetFileVersionInfoW(module.c_str(), 0, size, block_.data()) != FALSE);
        }

        [[nodiscard]] const VS_FIXEDFILEINFO &fixed() const
        {
            void *value = nullptr;
            UINT length = 0;
            REQUIRE(VerQueryValueW(block_.data(), L"\\", &value, &length) != FALSE);
            REQUIRE(length >= sizeof(VS_FIXEDFILEINFO));
            return *static_cast<const VS_FIXEDFILEINFO *>(value);
        }

        // The StringFileInfo block is US English / Unicode (040904B0), as Plugin.rc declares it.
        [[nodiscard]] std::wstring string(const std::wstring_view name) const
        {
            void *value = nullptr;
            UINT length = 0;
            const std::wstring key = L"\\StringFileInfo\\040904B0\\" + std::wstring{name};
            CAPTURE(key);
            REQUIRE(VerQueryValueW(block_.data(), key.c_str(), &value, &length) != FALSE);
            REQUIRE(length > 0);
            // `length` counts characters including the terminator.
            return std::wstring{static_cast<const wchar_t *>(value), length - 1};
        }

      private:
        std::vector<std::byte> block_;
    };

} // namespace

TEST_CASE("the plugin carries a VERSIONINFO resource that agrees with its generated identity and with pvdPluginInfo")
{
    const VersionBlock block{pvdkit::e2e::pluginPath()};

    const auto &fixed = block.fixed();
    CHECK(fixed.dwSignature == 0xFEEF04BD);
    CHECK(HIWORD(fixed.dwFileVersionMS) == PVDKIT_PLUGIN_VERSION_MAJOR);
    CHECK(LOWORD(fixed.dwFileVersionMS) == PVDKIT_PLUGIN_VERSION_MINOR);
    CHECK(HIWORD(fixed.dwFileVersionLS) == PVDKIT_PLUGIN_VERSION_PATCH);
    CHECK(LOWORD(fixed.dwFileVersionLS) == 0);
    CHECK(fixed.dwProductVersionMS == fixed.dwFileVersionMS);
    CHECK(fixed.dwProductVersionLS == fixed.dwFileVersionLS);
    CHECK(fixed.dwFileType == VFT_DLL);

    CHECK(block.string(L"FileVersion") == L"" PVDKIT_PLUGIN_VERSION);
    CHECK(block.string(L"ProductVersion") == L"" PVDKIT_PLUGIN_VERSION);
    CHECK(block.string(L"CompanyName") == L"" PVDKIT_PLUGIN_AUTHOR);
    CHECK(block.string(L"LegalCopyright") == L"" PVDKIT_PLUGIN_COPYRIGHT);
    CHECK(block.string(L"FileDescription") == L"" PVDKIT_PLUGIN_DESCRIPTION);
    CHECK(block.string(L"ProductName") == L"" PVDKIT_PLUGIN_FILENAME);
    CHECK(block.string(L"InternalName") == L"" PVDKIT_PLUGIN_FILENAME);
    CHECK(block.string(L"OriginalFilename") == L"" PVDKIT_PLUGIN_FILENAME);

    // The resource mirrors what the running plugin reports: the same version string and the same
    // comments (library versions) that pvdPluginInfo hands to the host.
    const auto plugin = pvdkit::e2e::loadInitializedPlugin();
    pvdInfoPlugin info{};
    plugin.exports().pluginInfo(&info);
    CHECK(block.string(L"FileVersion") == utf8ToWide(info.pVersion));
    CHECK(block.string(L"Comments") == utf8ToWide(info.pComments));
    CHECK(info.Priority == PVDKIT_PLUGIN_PRIORITY);
    CHECK(utf8ToWide(info.pName) == L"" PVDKIT_PLUGIN_NAME);
    plugin.exports().exit();
}
