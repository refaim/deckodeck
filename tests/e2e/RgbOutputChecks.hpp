#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "PluginHost.hpp"

namespace pvdkit::e2e
{
    namespace rgbChecks
    {

        struct PluginExit
        {
            const PluginExports &exports;

            ~PluginExit()
            {
                exports.exit();
            }
        };

        class FixtureDirectory
        {
          public:
            FixtureDirectory()
                : directory_(
                      std::filesystem::temp_directory_path() /
                      ("pvdkit-rgb-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64())))
            {
                REQUIRE(std::filesystem::create_directory(directory_));
            }

            ~FixtureDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(directory_, ignored);
            }

            FixtureDirectory(const FixtureDirectory &) = delete;
            FixtureDirectory &operator=(const FixtureDirectory &) = delete;

            void isolate(FixtureFile &file) const
            {
                const std::u8string utf8Source{file.utf8Path.begin(), file.utf8Path.end()};
                const std::filesystem::path source{utf8Source};
                const auto copy = directory_ / source.filename();
                REQUIRE(std::filesystem::copy_file(source, copy));
                const auto utf8Copy = copy.u8string();
                file.utf8Path.assign(utf8Copy.begin(), utf8Copy.end());
            }

          private:
            std::filesystem::path directory_;
        };

        class Session
        {
          public:
            Session(const PluginExports &exports, const FixtureFile &file, const OpenMode mode, const bool wide)
                : exports_(exports)
            {
                const auto headSize =
                    mode == OpenMode::Disk ? std::min(file.bytes.size(), kHostHeadSize) : file.bytes.size();
                const auto fileSize = mode == OpenMode::Disk ? static_cast<INT64>(file.bytes.size()) : INT64{0};
                const auto *head = reinterpret_cast<const BYTE *>(file.bytes.data());
                if (wide) {
                    const std::u8string utf8{file.utf8Path.begin(), file.utf8Path.end()};
                    const auto name = std::filesystem::path{utf8}.wstring();
                    REQUIRE(exports_.fileOpenW(name.c_str(), fileSize, head, static_cast<UINT32>(headSize),
                                               &image_.info, &image_.context) != FALSE);
                } else {
                    REQUIRE(exports_.fileOpen(file.utf8Path.c_str(), fileSize, head, static_cast<UINT32>(headSize),
                                              &image_.info, &image_.context) != FALSE);
                }
                REQUIRE(image_.context != nullptr);
            }

            ~Session()
            {
                exports_.fileClose(image_.context);
            }

            Session(const Session &) = delete;
            Session &operator=(const Session &) = delete;

            [[nodiscard]] const OpenedImage &image() const noexcept
            {
                return image_;
            }

          private:
            const PluginExports &exports_;
            OpenedImage image_{};
        };

        // This deliberately calls the ABI directly: native output must produce a runtime RED
        // before the older decodePage layout validator is taught the new wire codes.
        inline DecodedPage decodeRaw(const PluginExports &exports, void *context, const std::uint32_t pageIndex)
        {
            DecodedPage decoded;
            REQUIRE(exports.pageInfo(context, pageIndex, &decoded.page) != FALSE);
            REQUIRE(exports.pageDecode(context, pageIndex, &decoded.decode, nullptr, nullptr) != FALSE);
            REQUIRE(decoded.decode.pImage != nullptr);
            return decoded;
        }

        inline void checkImageInfo(const pvdInfoImage &actual, const pvdInfoImage &expected)
        {
            CHECK(actual.nPages == expected.nPages);
            CHECK(actual.Flags == expected.Flags);
            REQUIRE(actual.pFormatName != nullptr);
            REQUIRE(expected.pFormatName != nullptr);
            REQUIRE(actual.pCompression != nullptr);
            REQUIRE(expected.pCompression != nullptr);
            REQUIRE(actual.pComments != nullptr);
            REQUIRE(expected.pComments != nullptr);
            CHECK(std::string_view{actual.pFormatName} == expected.pFormatName);
            CHECK(std::string_view{actual.pCompression} == expected.pCompression);
            CHECK(std::string_view{actual.pComments} == expected.pComments);
        }

        inline void checkPageInfo(const pvdInfoPage &actual, const pvdInfoPage &expected)
        {
            CHECK(actual.lWidth == expected.lWidth);
            CHECK(actual.lHeight == expected.lHeight);
            CHECK(actual.nBPP == expected.nBPP);
            CHECK(actual.lFrameTime == expected.lFrameTime);
        }

        inline UINT32 nativeWireCode(const pvdInfoDecode &legacy) noexcept
        {
            return legacy.nBPP == 64 && (legacy.Flags & PVD_IDF_ALPHA) == 0 ? 49U : legacy.nBPP + 1U;
        }

        inline void checkPixelOrderEquivalent(const DecodedPage &actual, const DecodedPage &legacy)
        {
            REQUIRE((legacy.decode.nBPP == 24 || legacy.decode.nBPP == 32 || legacy.decode.nBPP == 64));
            REQUIRE((actual.decode.nBPP == legacy.decode.nBPP || actual.decode.nBPP == nativeWireCode(legacy.decode)));
            REQUIRE(actual.page.lWidth == legacy.page.lWidth);
            REQUIRE(actual.page.lHeight == legacy.page.lHeight);
            REQUIRE(isSupportedDecodeLayout(actual.page.lWidth, actual.decode.nBPP, actual.decode.lImagePitch));
            REQUIRE(isSupportedDecodeLayout(legacy.page.lWidth, legacy.decode.nBPP, legacy.decode.lImagePitch));
            auto expected = legacy.pixels();
            if (actual.decode.nBPP == 49) {
                std::vector<std::byte> rgb;
                rgb.reserve(expected.size() / 8U * 6U);
                for (std::size_t pixel = 0; pixel < expected.size(); pixel += 8U) {
                    rgb.insert(rgb.end(), {expected[pixel + 4U], expected[pixel + 5U], expected[pixel + 2U],
                                           expected[pixel + 3U], expected[pixel], expected[pixel + 1U]});
                }
                expected = std::move(rgb);
            } else if (actual.decode.nBPP != legacy.decode.nBPP) {
                const std::size_t sampleBytes = legacy.decode.nBPP == 64 ? 2U : 1U;
                const auto pixelBytes = static_cast<std::size_t>(legacy.bytesPerPixel());
                for (std::size_t pixel = 0; pixel < expected.size(); pixel += pixelBytes) {
                    for (std::size_t byte = 0; byte < sampleBytes; ++byte) {
                        std::swap(expected[pixel + byte], expected[pixel + 2U * sampleBytes + byte]);
                    }
                }
            }
            // Every retained byte is exact. RGB48 omits unused opaque A; RGBA preserves it.
            CHECK(actual.pixels() == expected);
        }

        inline void checkNativePage(const DecodedPage &actual, const DecodedPage &legacy)
        {
            REQUIRE(actual.decode.nBPP == nativeWireCode(legacy.decode));
            CHECK(static_cast<std::uint64_t>(actual.decode.lImagePitch) ==
                  static_cast<std::uint64_t>(actual.page.lWidth) * actual.bytesPerPixel());
            checkPageInfo(actual.page, legacy.page);
            CHECK(actual.decode.Flags == legacy.decode.Flags);
            CHECK(actual.decode.pPalette == nullptr);
            CHECK(actual.decode.nColorsUsed == 0);
            checkPixelOrderEquivalent(actual, legacy);
        }

        struct AbortLog
        {
            std::uint32_t calls = 0;
            bool validProgress = true;
        };

        inline BOOL __stdcall abortSecondCallback(void *context, const UINT32 step, const UINT32 steps)
        {
            auto &log = *static_cast<AbortLog *>(context);
            ++log.calls;
            log.validProgress = log.validProgress && steps > 0 && step < steps;
            return log.calls == 2 ? FALSE : TRUE;
        }

        inline void checkFileReleased(const FixtureFile &file)
        {
            const std::u8string utf8{file.utf8Path.begin(), file.utf8Path.end()};
            const auto path = std::filesystem::path{utf8};
            const auto handle =
                CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            REQUIRE(handle != INVALID_HANDLE_VALUE);
            CHECK(CloseHandle(handle) != FALSE);
        }

    } // namespace rgbChecks

    // No TEST_CASE is registered here: only the modules opting in to native RGB call this.
    template <class Fixtures> void checkNativeRgbOutputs(const Fixtures &accepted)
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const rgbChecks::PluginExit exit{exports};
        REQUIRE_FALSE(accepted.empty());
        bool sawRgb48 = false;
        for (const auto &fixture : accepted) {
            CAPTURE(fixture.name);
            auto file = readFixture(fixture.name);
            // Other test processes read the originals concurrently. Only this test owns the
            // copy, so its exclusive reopen can detect our session's leaked file mapping.
            const rgbChecks::FixtureDirectory isolated;
            isolated.isolate(file);
            {
                const rgbChecks::Session legacy{exports, file, OpenMode::Memory, false};
                const rgbChecks::Session disk{exports, file, OpenMode::Disk, true};
                const rgbChecks::Session memory{exports, file, OpenMode::Memory, true};
                rgbChecks::checkImageInfo(disk.image().info, legacy.image().info);
                rgbChecks::checkImageInfo(memory.image().info, legacy.image().info);
                REQUIRE(legacy.image().info.nPages > 0);
                UINT32 firstPageNativeCode = 0;
                for (std::uint32_t pageIndex = 0; pageIndex < legacy.image().info.nPages; ++pageIndex) {
                    CAPTURE(pageIndex);
                    auto legacyPage = rgbChecks::decodeRaw(exports, legacy.image().context, pageIndex);
                    auto diskPage = rgbChecks::decodeRaw(exports, disk.image().context, pageIndex);
                    auto memoryPage = rgbChecks::decodeRaw(exports, memory.image().context, pageIndex);
                    if (pageIndex == 0) {
                        firstPageNativeCode = rgbChecks::nativeWireCode(legacyPage.decode);
                    }
                    rgbChecks::checkNativePage(diskPage, legacyPage);
                    rgbChecks::checkNativePage(memoryPage, legacyPage);
                    sawRgb48 = sawRgb48 || diskPage.decode.nBPP == 49;
                    exports.pageFree(legacy.image().context, &legacyPage.decode);
                    exports.pageFree(disk.image().context, &diskPage.decode);
                    exports.pageFree(memory.image().context, &memoryPage.decode);
                }
                for (void *context : {disk.image().context, memory.image().context}) {
                    rgbChecks::AbortLog log;
                    pvdInfoDecode aborted{};
                    CHECK(exports.pageDecode(context, 0, &aborted, rgbChecks::abortSecondCallback, &log) == FALSE);
                    CHECK(aborted.pImage == nullptr);
                    CHECK(log.calls == 2);
                    CHECK(log.validProgress);
                    const auto retained = rgbChecks::decodeRaw(exports, context, 0);
                    CHECK(retained.decode.nBPP == firstPageNativeCode);
                    // Session close releases this successful decode without pvdPageFree.
                }
            }
            rgbChecks::checkFileReleased(file);
        }
        CHECK(sawRgb48);
    }

} // namespace pvdkit::e2e
