#pragma once

// Drives a built plugin (.pvd) exactly the way 0PictureView.dll does: LoadLibraryW, GetProcAddress of
// the eight exports, then the C ABI. Nothing here links against the plugin's static libraries.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pvd/PvdApi.hpp"

namespace pvdkit::e2e
{

    /// The eight entry points, typed exactly as the SDK header declares them, so a signature or
    /// calling-convention drift between the header and this driver fails to compile here.
    struct PluginExports
    {
        decltype(&pvdInit) init;
        decltype(&pvdExit) exit;
        decltype(&pvdPluginInfo) pluginInfo;
        decltype(&pvdFileOpen) fileOpen;
        decltype(&pvdPageInfo) pageInfo;
        decltype(&pvdPageDecode) pageDecode;
        decltype(&pvdPageFree) pageFree;
        decltype(&pvdFileClose) fileClose;
    };

    struct FreeLibraryDeleter
    {
        using pointer = HMODULE;
        void operator()(HMODULE module) const noexcept;
    };

    /// Owns the loaded module; FreeLibrary runs on destruction (after the test called pvdExit).
    class PluginLibrary
    {
      public:
        [[nodiscard]] static std::expected<PluginLibrary, std::string> load(const std::filesystem::path &path);

        [[nodiscard]] const PluginExports &exports() const noexcept
        {
            return exports_;
        }

      private:
        PluginLibrary(std::unique_ptr<HMODULE, FreeLibraryDeleter> module, PluginExports exports) noexcept;

        std::unique_ptr<HMODULE, FreeLibraryDeleter> module_;
        PluginExports exports_;
    };

    /// The path of the plugin this test binary was configured against (PVDKIT_PLUGIN_PATH).
    [[nodiscard]] std::filesystem::path pluginPath();

    /// Loads the plugin and calls pvdInit; REQUIREs both to succeed.
    [[nodiscard]] PluginLibrary loadInitializedPlugin();

    /// Where 0PictureView.dll hands over the whole file (`lFileSize == 0`, archive/virtual panel)
    /// or only its head (`lFileSize` = real size, `pBuf` = first 16 KiB, plugin opens by name).
    enum class OpenMode : std::uint8_t
    {
        Disk,
        Memory
    };

    constexpr std::size_t kHostHeadSize = 16 * 1024;

    struct FixtureFile
    {
        std::string utf8Path;
        std::vector<std::byte> bytes;
    };

    [[nodiscard]] std::filesystem::path fixturePath(std::string_view name);
    [[nodiscard]] FixtureFile readFixture(std::string_view name);

    struct OpenedImage
    {
        pvdInfoImage info{};
        void *context = nullptr;
    };

    /// Calls pvdFileOpen for `file` the way the host does in `mode`. In Memory mode `file.bytes` must
    /// outlive the returned context. Returns nullopt when the plugin answers FALSE.
    [[nodiscard]] std::optional<OpenedImage> openImage(const PluginExports &exports, const FixtureFile &file,
                                                       OpenMode mode);

    /// One decoded page plus the page information it was decoded against.
    struct DecodedPage
    {
        pvdInfoPage page{};
        pvdInfoDecode decode{};
        // PictureView allocates the undocumented x64 ICC fields immediately after pvdInfoDecode.
        // Keep matching storage in the host driver so an experiment DLL can be exercised safely.
        const BYTE *pIccProfile = nullptr;
        UINT32 cbIccProfile = 0;

        [[nodiscard]] std::uint32_t bytesPerPixel() const noexcept
        {
            return decode.nBPP / 8;
        }
        [[nodiscard]] std::span<const std::byte> row(std::uint32_t y) const noexcept;
        /// The `bytesPerPixel()` channel bytes of pixel (x, y): B, G, R[, A].
        [[nodiscard]] std::vector<std::uint8_t> pixel(std::uint32_t x, std::uint32_t y) const;
        /// All rows, tightly packed, for whole-image comparisons.
        [[nodiscard]] std::vector<std::byte> pixels() const;
    };

#if defined(_WIN64)
    static_assert(offsetof(DecodedPage, pIccProfile) - offsetof(DecodedPage, decode) ==
                  offsetof(pvdInfoDecodeEx, pIccProfile));
    static_assert(offsetof(DecodedPage, cbIccProfile) - offsetof(DecodedPage, decode) ==
                  offsetof(pvdInfoDecodeEx, cbIccProfile));
#endif

    /// Whether a positive top-down pitch can hold one row of a supported host pixel layout.
    /// The kit emits tight 24/32/64-bit rows; the host contract also permits padded 64-bit rows.
    [[nodiscard]] bool isSupportedDecodeLayout(std::uint32_t width, std::uint32_t bitsPerPixel,
                                               std::int32_t pitchBytes) noexcept;

    /// pvdPageInfo + pvdPageDecode for `page`; nullopt when either answers FALSE.
    [[nodiscard]] std::optional<DecodedPage> decodePage(const PluginExports &exports, void *context, std::uint32_t page,
                                                        pvdDecodeCallback callback, void *callbackContext);

} // namespace pvdkit::e2e
