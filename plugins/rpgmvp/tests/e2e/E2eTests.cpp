#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"
#include "PluginHost.hpp"

namespace pvdkit::e2e
{
    namespace
    {

        using Pixel = std::vector<std::uint8_t>;

        std::string text(const char *value)
        {
            return value == nullptr ? std::string{"<null>"} : std::string{value};
        }

        struct OpenedPage
        {
            OpenedImage image;
            DecodedPage page;
        };

        OpenedPage openAndDecode(const PluginExports &exports, const FixtureFile &file, const OpenMode mode)
        {
            auto image = openImage(exports, file, mode);
            REQUIRE(image.has_value());
            auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
            REQUIRE(page.has_value());
            return {*image, *page};
        }

        void freeAndClose(const PluginExports &exports, OpenedPage &opened)
        {
            exports.pageFree(opened.image.context, &opened.page.decode);
            exports.fileClose(opened.image.context);
        }

        struct CallbackLog
        {
            std::vector<std::uint32_t> steps;
            std::uint32_t abortAt = 0;
        };

        BOOL __stdcall progress(void *context, const UINT32 step, const UINT32)
        {
            auto &log = *static_cast<CallbackLog *>(context);
            log.steps.push_back(step);
            return log.steps.size() == log.abortAt ? FALSE : TRUE;
        }

        constexpr std::uint32_t outputBitsPerPixel(const rpgmvp::tests::FixtureExpectation &fixture)
        {
            if (fixture.sourceBpp > 32) {
                return 64;
            }
            return fixture.alpha ? 32U : 24U;
        }

        void checkDecodeFlags(const DecodedPage &decoded, const bool expectedAlpha)
        {
            const auto alphaFlags = expectedAlpha ? UINT32{PVD_IDF_ALPHA} : UINT32{0};
            CHECK(decoded.decode.Flags == alphaFlags);
        }

    } // namespace

    TEST_CASE("RPGMVP.pvd identifies itself through the eight-export host boundary")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
        pvdInfoPlugin info{};
        exports.pluginInfo(&info);
        CHECK(info.Priority == 10);
        CHECK(text(info.pName) == "RPGMVP");
        CHECK(text(info.pVersion) == "1.1.0");
        CHECK(text(info.pComments).find("libspng 0.7.4") != std::string::npos);
        CHECK(text(info.pComments).find("zlib 1.3.2") != std::string::npos);
        CHECK(text(info.pComments).find("static build") != std::string::npos);
        CHECK_FALSE(text(info.pComments).empty());
        exports.pluginInfo(nullptr);
        exports.exit();
    }

    TEST_CASE("every accepted RPGMVP fixture decodes identically from disk and memory")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        for (const auto &expected : rpgmvp::tests::kAccepted) {
            CAPTURE(expected.name);
            const auto file = readFixture(expected.name);
            auto disk = openAndDecode(exports, file, OpenMode::Disk);
            auto memory = openAndDecode(exports, file, OpenMode::Memory);
            CHECK(disk.image.info.nPages == 1);
            CHECK(disk.image.info.Flags == 0);
            CHECK(text(disk.image.info.pFormatName) == "RPGMVP");
            CHECK(text(disk.image.info.pCompression) == "Deflate");
            CHECK_FALSE(text(disk.image.info.pComments).empty());
            CHECK(disk.page.page.lWidth == expected.width);
            CHECK(disk.page.page.lHeight == expected.height);
            CHECK(disk.page.page.nBPP == expected.sourceBpp);
            CHECK(disk.page.page.lFrameTime == 0);
            const auto expectedOutputBpp = outputBitsPerPixel(expected);
            CHECK(disk.page.decode.nBPP == expectedOutputBpp);
            CHECK(disk.page.decode.lImagePitch == static_cast<INT32>(expected.width * (expectedOutputBpp / 8U)));
            CHECK(disk.page.decode.pPalette == nullptr);
            CHECK(disk.page.decode.nColorsUsed == 0);
            checkDecodeFlags(disk.page, expected.alpha);
            CHECK(memory.page.decode.Flags == disk.page.decode.Flags);
            checkDecodeFlags(memory.page, expected.alpha);
            CHECK(memory.page.pixels() == disk.page.pixels());
            CHECK(text(memory.image.info.pComments) == text(disk.image.info.pComments));
            freeAndClose(exports, disk);
            freeAndClose(exports, memory);
        }
        exports.exit();
    }

    TEST_CASE("synthetic greyscale and 16-bit fixtures produce exact BGR values")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        auto gray = openAndDecode(exports, readFixture("gray8_16x8.rpgmvp"), OpenMode::Memory);
        CHECK(gray.page.pixel(0, 0) == Pixel{0, 0, 0});
        CHECK(gray.page.pixel(1, 0) == Pixel{16, 16, 16});
        CHECK(gray.page.pixel(15, 7) == Pixel{240, 240, 240});
        freeAndClose(exports, gray);

        auto oneBit = openAndDecode(exports, readFixture("gray1_16x8.rpgmvp"), OpenMode::Disk);
        CHECK(oneBit.page.pixel(0, 0) == Pixel{0, 0, 0});
        CHECK(oneBit.page.pixel(8, 0) == Pixel{255, 255, 255});
        freeAndClose(exports, oneBit);

        auto grayAlpha = openAndDecode(exports, readFixture("graya8_16x8.rpgmvp"), OpenMode::Memory);
        CHECK(grayAlpha.page.pixel(2, 1) == Pixel{19, 19, 19, 32});
        freeAndClose(exports, grayAlpha);

        auto palette = openAndDecode(exports, readFixture("indexed4_trns_144x192_cursor.rpgmvp"), OpenMode::Memory);
        CHECK(palette.page.pixel(0, 0) == Pixel{0, 0, 0, 0});
        CHECK(palette.page.pixel(17, 0) == Pixel{78, 224, 255, 222});
        freeAndClose(exports, palette);

        auto rgba16 = openAndDecode(exports, readFixture("rgba16_60x20_par.rpgmvp"), OpenMode::Disk);
        CHECK(rgba16.page.decode.nBPP == 64);
        CHECK(rgba16.page.decode.lImagePitch == 480);
        checkDecodeFlags(rgba16.page, true);
        CHECK(rgba16.page.pixel(0, 0) == Pixel{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00});
        freeAndClose(exports, rgba16);

        auto rgb16 = openAndDecode(exports, readFixture("rgb16_88x4a.rpgmvp"), OpenMode::Memory);
        CHECK(rgb16.page.decode.nBPP == 64);
        CHECK(rgb16.page.decode.lImagePitch == 704);
        checkDecodeFlags(rgb16.page, false);
        CHECK(rgb16.page.pixel(0, 0) == Pixel{0xF4, 0x3B, 0x60, 0x60, 0x96, 0x1B, 0xFF, 0xFF});
        freeAndClose(exports, rgb16);
        exports.exit();
    }

    TEST_CASE("progress callbacks can abort at every stage and the session remains usable")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("rgba8_48x48.rpgmvp");
        auto image = openImage(exports, file, OpenMode::Memory);
        REQUIRE(image.has_value());

        CallbackLog complete;
        auto reference = decodePage(exports, image->context, 0, progress, &complete);
        REQUIRE(reference.has_value());
        CHECK(complete.steps == std::vector<std::uint32_t>{0, 1, 2});
        const auto pixels = reference->pixels();
        exports.pageFree(image->context, &reference->decode);

        for (const auto abortAt : {1U, 2U, 3U}) {
            CallbackLog aborting{{}, abortAt};
            pvdInfoDecode decode{};
            CHECK(exports.pageDecode(image->context, 0, &decode, progress, &aborting) == FALSE);
            CHECK(aborting.steps.size() == abortAt);
            CHECK(decode.pImage == nullptr);
            exports.pageFree(image->context, &decode);
        }
        auto again = decodePage(exports, image->context, 0, nullptr, nullptr);
        REQUIRE(again.has_value());
        CHECK(again->pixels() == pixels);
        exports.pageFree(image->context, &again->decode);
        exports.fileClose(image->context);
        exports.exit();
    }

    TEST_CASE("invalid pages and inputs fail cleanly and closing owns outstanding pages")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("rgba8_48x48.rpgmvp");
        auto image = openImage(exports, file, OpenMode::Memory);
        REQUIRE(image.has_value());
        pvdInfoPage page{};
        pvdInfoDecode decode{};
        CHECK(exports.pageInfo(image->context, 1, &page) == FALSE);
        CHECK(exports.pageDecode(image->context, 1, &decode, nullptr, nullptr) == FALSE);
        CHECK(exports.pageInfo(image->context, 0, nullptr) == FALSE);
        CHECK(exports.pageDecode(image->context, 0, nullptr, nullptr, nullptr) == FALSE);
        REQUIRE(decodePage(exports, image->context, 0, nullptr, nullptr).has_value());
        exports.pageFree(image->context, nullptr);
        exports.fileClose(image->context);

        for (const auto name : rpgmvp::tests::kRejected) {
            CAPTURE(name);
            const auto rejected = readFixture(name);
            CHECK_FALSE(openImage(exports, rejected, OpenMode::Disk).has_value());
            CHECK_FALSE(openImage(exports, rejected, OpenMode::Memory).has_value());
        }

        for (const auto name : rpgmvp::tests::kDecodeFailures) {
            const auto truncated = readFixture(name);
            auto corrupt = openImage(exports, truncated, OpenMode::Memory);
            REQUIRE(corrupt.has_value());
            CHECK_FALSE(decodePage(exports, corrupt->context, 0, nullptr, nullptr).has_value());
            exports.fileClose(corrupt->context);
        }
        exports.exit();
    }

    TEST_CASE("independent threads decode the same encrypted image identically")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("rgba8_36x16_shadow2.rpgmvp");
        auto baseline = openAndDecode(exports, file, OpenMode::Disk);
        const auto expected = baseline.page.pixels();
        freeAndClose(exports, baseline);

        constexpr std::size_t kThreads = 4;
        std::latch start{kThreads};
        std::array<std::vector<std::byte>, kThreads> results;
        std::array<bool, kThreads> succeeded{};
        {
            std::vector<std::jthread> threads;
            threads.reserve(kThreads);
            for (std::size_t index = 0; index < kThreads; ++index) {
                threads.emplace_back([&, index] {
                    start.arrive_and_wait();
                    auto image = openImage(exports, file, OpenMode::Memory);
                    if (!image) {
                        return;
                    }
                    auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
                    if (page) {
                        results[index] = page->pixels();
                        succeeded[index] = true;
                        exports.pageFree(image->context, &page->decode);
                    }
                    exports.fileClose(image->context);
                });
            }
        }
        for (std::size_t index = 0; index < kThreads; ++index) {
            CAPTURE(index);
            CHECK(succeeded[index]);
            CHECK(results[index] == expected);
        }
        exports.exit();
    }

} // namespace pvdkit::e2e
