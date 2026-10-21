#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "PluginHost.hpp"
#include "pvd/PvdApi.hpp"

namespace pvdkit::e2e
{
    namespace
    {

        namespace fs = std::filesystem;

        std::string text(const char *value)
        {
            return value == nullptr ? std::string{"<null>"} : std::string{value};
        }

        std::string utf8Path(const fs::path &path)
        {
            const auto utf8 = path.u8string();
            return std::string{utf8.begin(), utf8.end()};
        }

        fs::path uniqueTemporaryDirectory(const std::string_view label)
        {
            return fs::temp_directory_path() /
                   (L"pvdkit-svg-" + std::wstring{label.begin(), label.end()} + L"-" +
                    std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        }

        class TemporaryDirectory
        {
          public:
            explicit TemporaryDirectory(const std::string_view label) : path_(uniqueTemporaryDirectory(label))
            {
                REQUIRE(fs::create_directory(path_));
            }

            ~TemporaryDirectory()
            {
                try {
                    std::error_code ignored;
                    fs::remove_all(path_, ignored);
                } catch (...) {
                    return;
                }
            }

            TemporaryDirectory(const TemporaryDirectory &) = delete;
            TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

            [[nodiscard]] const fs::path &path() const noexcept
            {
                return path_;
            }

          private:
            fs::path path_;
        };

        class CurrentDirectoryGuard
        {
          public:
            explicit CurrentDirectoryGuard(const fs::path &directory) : previous_(fs::current_path())
            {
                fs::current_path(directory);
            }

            ~CurrentDirectoryGuard()
            {
                std::error_code ignored;
                fs::current_path(previous_, ignored);
            }

            CurrentDirectoryGuard(const CurrentDirectoryGuard &) = delete;
            CurrentDirectoryGuard &operator=(const CurrentDirectoryGuard &) = delete;

          private:
            fs::path previous_;
        };

        class ResourceFixture
        {
          public:
            ResourceFixture(const std::string_view label, const std::string_view asset)
                : directory_(label), svg_(directory_.path() / L"resource.svg")
            {
                REQUIRE(fs::copy_file(fixturePath("resource.svg"), svg_));
                REQUIRE(fs::copy_file(fixturePath(asset), directory_.path() / L"resource.png"));
                file_.bytes = readFixture("resource.svg").bytes;
                file_.utf8Path = utf8Path(svg_);
            }

            [[nodiscard]] const FixtureFile &file() const noexcept
            {
                return file_;
            }

          private:
            TemporaryDirectory directory_;
            fs::path svg_;
            FixtureFile file_;
        };

        class UnicodeCopy
        {
          public:
            explicit UnicodeCopy(const std::string_view fixture)
                : directory_(fs::temp_directory_path() /
                             (L"pvdkit-svg-wide-" +
                              std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()))),
                  path_(directory_ / L"\u0420\u0438\u0441\u0443\u043d\u043e\u043a-\U0001F600.svg")
            {
                REQUIRE(fs::create_directory(directory_));
                REQUIRE(fs::copy_file(fixturePath(fixture), path_));
            }

            ~UnicodeCopy()
            {
                try {
                    std::error_code ignored;
                    fs::remove_all(directory_, ignored);
                } catch (...) {
                    return;
                }
            }

            UnicodeCopy(const UnicodeCopy &) = delete;
            UnicodeCopy &operator=(const UnicodeCopy &) = delete;

            [[nodiscard]] const fs::path &path() const noexcept
            {
                return path_;
            }

          private:
            fs::path directory_;
            fs::path path_;
        };

        std::optional<OpenedImage> openWideImage(const PluginExports &exports, const FixtureFile &file,
                                                 const std::wstring &name, const OpenMode mode)
        {
            const auto headSize =
                mode == OpenMode::Disk ? std::min(file.bytes.size(), kHostHeadSize) : file.bytes.size();
            const auto fileSize = mode == OpenMode::Disk ? static_cast<INT64>(file.bytes.size()) : INT64{0};
            OpenedImage opened;
            const BOOL accepted =
                exports.fileOpenW(name.c_str(), fileSize, reinterpret_cast<const BYTE *>(file.bytes.data()),
                                  static_cast<UINT32>(headSize), &opened.info, &opened.context);
            if (accepted == FALSE) {
                return std::nullopt;
            }
            REQUIRE(opened.context != nullptr);
            return opened;
        }

        struct AbortLog
        {
            std::vector<std::uint32_t> steps;
            std::vector<std::uint32_t> totals;
            std::uint32_t abortStep = 0;
        };

        BOOL __stdcall progress(void *context, const UINT32 step, const UINT32 steps)
        {
            auto &log = *static_cast<AbortLog *>(context);
            log.steps.push_back(step);
            log.totals.push_back(steps);
            return step == log.abortStep ? FALSE : TRUE;
        }

        using Pixel = std::array<std::uint8_t, 4>;

        constexpr std::array<Pixel, 4> kLegacyBasicPixels{
            Pixel{0, 0, 255, 255},
            Pixel{0, 255, 0, 255},
            Pixel{255, 0, 0, 255},
            Pixel{0, 255, 255, 255},
        };
        constexpr std::array<Pixel, 4> kNativeBasicPixels{
            Pixel{255, 0, 0, 255},
            Pixel{0, 255, 0, 255},
            Pixel{0, 0, 255, 255},
            Pixel{255, 255, 0, 255},
        };
        // checker-2x2.png scaled to 48x48 is yellow at (24,24), while resource.png has straight
        // black alpha 131 at (24,40). A skipped external image is fully transparent.
        constexpr Pixel kCheckerResourcePixel{0, 255, 255, 255};
        constexpr Pixel kOriginalResourcePixel{0, 0, 0, 131};
        constexpr Pixel kTransparentPixel{0, 0, 0, 0};

        void checkBasicPixels(const DecodedPage &page, const std::uint32_t wireBpp, const std::array<Pixel, 4> &pixels)
        {
            CHECK(page.page.lWidth == 2);
            CHECK(page.page.lHeight == 2);
            // PageInfo always reports the real sample depth (8 bits per channel); only the decoded
            // buffer carries the RGB/RGBA wire code selected by the entry point.
            CHECK(page.page.nBPP == 32);
            CHECK(page.decode.nBPP == wireBpp);
            CHECK(page.decode.Flags == PVD_IDF_ALPHA);
            CHECK(page.decode.lImagePitch == 8);
            CHECK(page.pixel(0, 0) == std::vector<std::uint8_t>{pixels[0].begin(), pixels[0].end()});
            CHECK(page.pixel(1, 0) == std::vector<std::uint8_t>{pixels[1].begin(), pixels[1].end()});
            CHECK(page.pixel(0, 1) == std::vector<std::uint8_t>{pixels[2].begin(), pixels[2].end()});
            CHECK(page.pixel(1, 1) == std::vector<std::uint8_t>{pixels[3].begin(), pixels[3].end()});
        }

        void checkResourcePixel(const DecodedPage &page, const std::uint32_t x, const std::uint32_t y,
                                const Pixel &expected)
        {
            CHECK(page.page.lWidth == 48);
            CHECK(page.page.lHeight == 48);
            CHECK(page.page.nBPP == 32);
            CHECK(page.decode.nBPP == 32);
            CHECK(page.decode.Flags == PVD_IDF_ALPHA);
            CHECK(page.decode.lImagePitch == 192);
            CHECK(page.pixel(x, y) == std::vector<std::uint8_t>{expected.begin(), expected.end()});
        }

        bool opensWithPixel(const PluginExports &exports, const FixtureFile &file, const std::uint32_t x,
                            const std::uint32_t y, const Pixel &expected)
        {
            pvdInfoImage image{};
            void *context = nullptr;
            const auto headSize = std::min(file.bytes.size(), kHostHeadSize);
            if (exports.fileOpen(file.utf8Path.c_str(), static_cast<INT64>(file.bytes.size()),
                                 reinterpret_cast<const BYTE *>(file.bytes.data()), static_cast<UINT32>(headSize),
                                 &image, &context) == FALSE ||
                context == nullptr || image.nPages != 1) {
                exports.fileClose(context);
                return false;
            }

            pvdInfoPage page{};
            pvdInfoDecode decoded{};
            bool matches = exports.pageInfo(context, 0, &page) != FALSE && page.lWidth == 48 && page.lHeight == 48 &&
                           exports.pageDecode(context, 0, &decoded, nullptr, nullptr) != FALSE &&
                           decoded.pImage != nullptr && decoded.nBPP == 32 && decoded.lImagePitch == 192 &&
                           decoded.Flags == PVD_IDF_ALPHA;
            if (matches) {
                const auto *const pixels = reinterpret_cast<const std::uint8_t *>(decoded.pImage);
                const auto offset = static_cast<std::size_t>(y * static_cast<std::uint32_t>(decoded.lImagePitch)) +
                                    static_cast<std::size_t>(x) * 4U;
                matches = std::ranges::equal(expected, std::span{pixels + offset, 4U});
            }
            if (decoded.pImage != nullptr) {
                exports.pageFree(context, &decoded);
            }
            exports.fileClose(context);
            return matches;
        }

    } // namespace

    TEST_CASE("SVG.pvd loads its nine exports and opens valid SVG and SVGZ from disk and memory")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        for (const auto name : {"basic-2x2-colors.svg", "valid.svgz"}) {
            INFO(std::string{name});
            const auto file = readFixture(name);
            for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
                INFO(std::string{mode == OpenMode::Disk ? "disk" : "memory"});
                const auto image = openImage(exports, file, mode);
                REQUIRE(image.has_value());
                CHECK(image->info.nPages == 1);
                CHECK(image->info.Flags == 0);
                CHECK(text(image->info.pFormatName) == "SVG");
                auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
                REQUIRE(page.has_value());
                checkBasicPixels(*page, 32, kLegacyBasicPixels);
                exports.pageFree(image->context, &page->decode);
                exports.fileClose(image->context);
            }
        }
        exports.exit();
    }

    TEST_CASE("SVG.pvd accepts valid SVG through the UTF-16 entry point in disk and memory modes")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const UnicodeCopy copy{"basic-2x2-colors.svg"};
        const auto file = readFixture("basic-2x2-colors.svg");
        const auto wide = copy.path().wstring();
        for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
            INFO(std::string{mode == OpenMode::Disk ? "disk" : "memory"});
            const auto image = openWideImage(exports, file, wide, mode);
            REQUIRE(image.has_value());
            auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
            REQUIRE(page.has_value());
            checkBasicPixels(*page, 33, kNativeBasicPixels);
            exports.pageFree(image->context, &page->decode);
            exports.fileClose(image->context);
        }
        exports.exit();
    }

    TEST_CASE("SVG.pvd rejects invalid page bounds and supports progress aborts without losing the session")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("basic-2x2-colors.svg");
        const auto image = openImage(exports, file, OpenMode::Disk);
        REQUIRE(image.has_value());

        pvdInfoPage bounds{};
        CHECK(exports.pageInfo(image->context, 1, &bounds) == FALSE);
        pvdInfoDecode missing{};
        CHECK(exports.pageDecode(image->context, 1, &missing, nullptr, nullptr) == FALSE);

        for (const std::uint32_t abortStep : {0U, 1U, 2U}) {
            INFO(abortStep);
            AbortLog log{{}, {}, abortStep};
            pvdInfoDecode aborted{};
            CHECK(exports.pageDecode(image->context, 0, &aborted, progress, &log) == FALSE);
            std::vector<std::uint32_t> expectedSteps;
            for (std::uint32_t step = 0; step <= abortStep; ++step) {
                expectedSteps.push_back(step);
            }
            CHECK(log.steps == expectedSteps);
            CHECK(log.totals == std::vector<std::uint32_t>(expectedSteps.size(), 3U));
            CHECK(aborted.pImage == nullptr);
            CHECK(aborted.pPalette == nullptr);
            CHECK(aborted.Flags == 0);
            CHECK(aborted.nBPP == 0);
            CHECK(aborted.nColorsUsed == 0);
            CHECK(aborted.lImagePitch == 0);
            CHECK(aborted.pICC == nullptr);
            CHECK(aborted.lICC == 0);
            auto retried = decodePage(exports, image->context, 0, nullptr, nullptr);
            REQUIRE(retried.has_value());
            checkBasicPixels(*retried, 32, kLegacyBasicPixels);
            exports.pageFree(image->context, &retried->decode);
        }
        exports.fileClose(image->context);
        exports.exit();
    }

    TEST_CASE("SVG.pvd resolves disk and memory resources independently in one plugin state")
    {
        const ResourceFixture checker{"resource-a", "assets/checker-2x2.png"};
        const ResourceFixture original{"resource-b", "resource.png"};
        const TemporaryDirectory emptyWorkingDirectory{"resource-cwd"};
        const CurrentDirectoryGuard workingDirectory{emptyWorkingDirectory.path()};

        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        auto diskChecker = openImage(exports, checker.file(), OpenMode::Disk);
        REQUIRE(diskChecker.has_value());
        auto checkerPage = decodePage(exports, diskChecker->context, 0, nullptr, nullptr);
        REQUIRE(checkerPage.has_value());
        checkResourcePixel(*checkerPage, 24, 24, kCheckerResourcePixel);
        exports.pageFree(diskChecker->context, &checkerPage->decode);

        // The memory session has no source path. It must not inherit resource-a from the live
        // disk session, and the empty working directory must not accidentally satisfy the href.
        auto memory = openImage(exports, checker.file(), OpenMode::Memory);
        REQUIRE(memory.has_value());
        auto memoryPage = decodePage(exports, memory->context, 0, nullptr, nullptr);
        REQUIRE(memoryPage.has_value());
        checkResourcePixel(*memoryPage, 24, 24, kTransparentPixel);
        exports.pageFree(memory->context, &memoryPage->decode);

        auto diskOriginal = openImage(exports, original.file(), OpenMode::Disk);
        REQUIRE(diskOriginal.has_value());
        auto originalPage = decodePage(exports, diskOriginal->context, 0, nullptr, nullptr);
        REQUIRE(originalPage.has_value());
        checkResourcePixel(*originalPage, 24, 40, kOriginalResourcePixel);
        exports.pageFree(diskOriginal->context, &originalPage->decode);

        auto memoryAfterOriginal = openImage(exports, original.file(), OpenMode::Memory);
        REQUIRE(memoryAfterOriginal.has_value());
        auto memoryAfterOriginalPage = decodePage(exports, memoryAfterOriginal->context, 0, nullptr, nullptr);
        REQUIRE(memoryAfterOriginalPage.has_value());
        checkResourcePixel(*memoryAfterOriginalPage, 24, 40, kTransparentPixel);
        exports.pageFree(memoryAfterOriginal->context, &memoryAfterOriginalPage->decode);

        auto diskCheckerAgain = openImage(exports, checker.file(), OpenMode::Disk);
        REQUIRE(diskCheckerAgain.has_value());
        auto checkerAgainPage = decodePage(exports, diskCheckerAgain->context, 0, nullptr, nullptr);
        REQUIRE(checkerAgainPage.has_value());
        checkResourcePixel(*checkerAgainPage, 24, 24, kCheckerResourcePixel);
        exports.pageFree(diskCheckerAgain->context, &checkerAgainPage->decode);

        exports.fileClose(diskChecker->context);
        exports.fileClose(memory->context);
        exports.fileClose(diskOriginal->context);
        exports.fileClose(memoryAfterOriginal->context);
        exports.fileClose(diskCheckerAgain->context);
        exports.exit();
    }

    TEST_CASE("SVG.pvd keeps relative resources isolated across concurrent disk sessions")
    {
        const ResourceFixture checker{"resource-thread-a", "assets/checker-2x2.png"};
        const ResourceFixture original{"resource-thread-b", "resource.png"};
        const TemporaryDirectory emptyWorkingDirectory{"resource-thread-cwd"};
        const CurrentDirectoryGuard workingDirectory{emptyWorkingDirectory.path()};

        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        constexpr std::size_t workerCount = 8;
        constexpr std::size_t iterations = 4;
        std::vector<int> matches(workerCount, 0);
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&, worker] {
                bool workerMatches = true;
                for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
                    if (worker % 2U == 0U) {
                        workerMatches =
                            workerMatches && opensWithPixel(exports, checker.file(), 24, 24, kCheckerResourcePixel);
                    } else {
                        workerMatches =
                            workerMatches && opensWithPixel(exports, original.file(), 24, 40, kOriginalResourcePixel);
                    }
                }
                matches[worker] = workerMatches ? 1 : 0;
            });
        }
        for (auto &worker : workers) {
            worker.join();
        }
        for (const int workerMatches : matches) {
            CHECK(workerMatches == 1);
        }
        exports.exit();
    }

    TEST_CASE("SVG.pvd rejects malformed non-SVG invalid UTF-8 and truncated SVGZ in disk and memory modes")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        for (const auto name : {"malformed.svg", "not-svg.svg", "invalid-utf8.svg", "truncated.svgz"}) {
            INFO(std::string{name});
            const auto file = readFixture(name);
            CHECK_FALSE(openImage(exports, file, OpenMode::Disk).has_value());
            CHECK_FALSE(openImage(exports, file, OpenMode::Memory).has_value());
        }
        exports.exit();
    }

} // namespace pvdkit::e2e
