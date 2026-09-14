#include "PluginHost.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <string>
#include <utility>

#include <doctest/doctest.h>

namespace pvdkit::e2e
{
    namespace
    {

        std::string win32Failure(const std::string_view operation, const DWORD error)
        {
            return std::string{operation} + " failed (Win32 error " + std::to_string(error) + ")";
        }

        template <class Fn> [[nodiscard]] std::expected<Fn, std::string> resolve(const HMODULE module, const char *name)
        {
            // GetProcAddress hands back a generic FARPROC; converting it to the SDK's declared signature is
            // the host's own idiom. std::bit_cast between the two (same-sized) function pointer types says
            // so without clang's function-type-mismatch warning on a direct reinterpret_cast; the other
            // spellings are objected to as well (a void* intermediate by bugprone-casting-through-void, an
            // integer one by performance-no-int-to-ptr, memcpy by the same check as bit_cast), which is
            // why tests/.clang-tidy disables bugprone-bitwise-pointer-cast for test code.
            const auto address = GetProcAddress(module, name);
            if (address == nullptr) {
                return std::unexpected(win32Failure(std::string{"GetProcAddress("} + name + ")", GetLastError()));
            }
            return std::bit_cast<Fn>(address);
        }

    } // namespace

    void FreeLibraryDeleter::operator()(const HMODULE module) const noexcept
    {
        static_cast<void>(FreeLibrary(module));
    }

    PluginLibrary::PluginLibrary(std::unique_ptr<HMODULE, FreeLibraryDeleter> module,
                                 const PluginExports exports) noexcept
        : module_(std::move(module)), exports_(exports)
    {
    }

    std::expected<PluginLibrary, std::string> PluginLibrary::load(const std::filesystem::path &path)
    {
        std::unique_ptr<HMODULE, FreeLibraryDeleter> module{LoadLibraryW(path.c_str())};
        if (!module) {
            return std::unexpected(win32Failure("LoadLibraryW(" + path.string() + ")", GetLastError()));
        }
        PluginExports exports{};
        const auto init = resolve<decltype(&pvdInit)>(module.get(), "pvdInit");
        const auto exit = resolve<decltype(&pvdExit)>(module.get(), "pvdExit");
        const auto pluginInfo = resolve<decltype(&pvdPluginInfo)>(module.get(), "pvdPluginInfo");
        const auto fileOpen = resolve<decltype(&pvdFileOpen)>(module.get(), "pvdFileOpen");
        const auto pageInfo = resolve<decltype(&pvdPageInfo)>(module.get(), "pvdPageInfo");
        const auto pageDecode = resolve<decltype(&pvdPageDecode)>(module.get(), "pvdPageDecode");
        const auto pageFree = resolve<decltype(&pvdPageFree)>(module.get(), "pvdPageFree");
        const auto fileClose = resolve<decltype(&pvdFileClose)>(module.get(), "pvdFileClose");
        for (const auto &failure :
             {init.error_or(""), exit.error_or(""), pluginInfo.error_or(""), fileOpen.error_or(""),
              pageInfo.error_or(""), pageDecode.error_or(""), pageFree.error_or(""), fileClose.error_or("")}) {
            if (!failure.empty()) {
                return std::unexpected(failure);
            }
        }
        exports.init = *init;
        exports.exit = *exit;
        exports.pluginInfo = *pluginInfo;
        exports.fileOpen = *fileOpen;
        exports.pageInfo = *pageInfo;
        exports.pageDecode = *pageDecode;
        exports.pageFree = *pageFree;
        exports.fileClose = *fileClose;
        return PluginLibrary{std::move(module), exports};
    }

    std::filesystem::path pluginPath()
    {
        return std::filesystem::path{PVDKIT_PLUGIN_PATH};
    }

    PluginLibrary loadInitializedPlugin()
    {
        auto loaded = PluginLibrary::load(pluginPath());
        const std::string loadError = loaded ? std::string{} : loaded.error();
        CAPTURE(loadError);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->exports().init() == PVD_CURRENT_INTERFACE_VERSION);
        return std::move(*loaded);
    }

    std::filesystem::path fixturePath(const std::string_view name)
    {
        return std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
    }

    FixtureFile readFixture(const std::string_view name)
    {
        const auto path = fixturePath(name);
        FixtureFile file;
        const auto utf8 = path.u8string();
        file.utf8Path.assign(utf8.begin(), utf8.end());
        file.bytes.resize(static_cast<std::size_t>(std::filesystem::file_size(path)));
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        stream.read(reinterpret_cast<char *>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
        REQUIRE(stream.good());
        return file;
    }

    std::optional<OpenedImage> openImage(const PluginExports &exports, const FixtureFile &file, const OpenMode mode)
    {
        // Disk: the host reports the real size and hands over at most the first 16 KiB; the plugin
        // opens the file by name. Memory: size 0, and the buffer is the whole file.
        const auto headSize = mode == OpenMode::Disk ? std::min(file.bytes.size(), kHostHeadSize) : file.bytes.size();
        const auto fileSize = mode == OpenMode::Disk ? static_cast<INT64>(file.bytes.size()) : INT64{0};
        OpenedImage opened;
        const BOOL accepted =
            exports.fileOpen(file.utf8Path.c_str(), fileSize, reinterpret_cast<const BYTE *>(file.bytes.data()),
                             static_cast<UINT32>(headSize), &opened.info, &opened.context);
        if (accepted == FALSE) {
            return std::nullopt;
        }
        REQUIRE(opened.context != nullptr);
        return opened;
    }

    std::span<const std::byte> DecodedPage::row(const std::uint32_t y) const noexcept
    {
        const auto pitch = static_cast<std::size_t>(decode.lImagePitch);
        return {reinterpret_cast<const std::byte *>(decode.pImage) + y * pitch,
                static_cast<std::size_t>(page.lWidth) * bytesPerPixel()};
    }

    std::vector<std::uint8_t> DecodedPage::pixel(const std::uint32_t x, const std::uint32_t y) const
    {
        const auto bytes = row(y).subspan(static_cast<std::size_t>(x) * bytesPerPixel(), bytesPerPixel());
        std::vector<std::uint8_t> channels(bytes.size());
        std::ranges::transform(bytes, channels.begin(),
                               [](const std::byte b) { return std::to_integer<std::uint8_t>(b); });
        return channels;
    }

    std::vector<std::byte> DecodedPage::pixels() const
    {
        std::vector<std::byte> all;
        all.reserve(static_cast<std::size_t>(page.lHeight) * page.lWidth * bytesPerPixel());
        for (std::uint32_t y = 0; y < page.lHeight; ++y) {
            const auto bytes = row(y);
            all.insert(all.end(), bytes.begin(), bytes.end());
        }
        return all;
    }

    bool isSupportedDecodeLayout(const std::uint32_t width, const std::uint32_t bitsPerPixel,
                                 const std::int32_t pitchBytes) noexcept
    {
        if (pitchBytes <= 0) {
            return false;
        }
        const auto pitch = static_cast<std::uint64_t>(pitchBytes);
        if (bitsPerPixel == 64) {
            return pitch >= static_cast<std::uint64_t>(width) * 8U;
        }
        if (bitsPerPixel == 24 || bitsPerPixel == 32) {
            return pitch == static_cast<std::uint64_t>(width) * (bitsPerPixel / 8U);
        }
        return false;
    }

    std::optional<DecodedPage> decodePage(const PluginExports &exports, void *context, const std::uint32_t page,
                                          const pvdDecodeCallback callback, void *callbackContext)
    {
        DecodedPage decoded;
        if (exports.pageInfo(context, page, &decoded.page) == FALSE) {
            return std::nullopt;
        }
        if (exports.pageDecode(context, page, &decoded.decode, callback, callbackContext) == FALSE) {
            return std::nullopt;
        }
        // The plugin promises top-down rows and a writable buffer of its own. The established
        // The kit emits tight 24/32/64-bit layouts; accept host-valid padding on the 64-bit path.
        REQUIRE(decoded.decode.pImage != nullptr);
        REQUIRE(isSupportedDecodeLayout(decoded.page.lWidth, decoded.decode.nBPP, decoded.decode.lImagePitch));
        return decoded;
    }

} // namespace pvdkit::e2e
