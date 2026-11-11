#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "PluginHost.hpp"

namespace pvdkit::e2e
{
    namespace
    {

        struct WebpCase
        {
            std::string_view name;
            std::uint32_t side;
            bool alpha;
            bool lossless;
            std::array<std::uint8_t, 4> first;
        };

        std::optional<OpenedImage> openWebpWide(const PluginExports &exports, const FixtureFile &file,
                                                const std::string_view name, const OpenMode mode)
        {
            const auto bytes = std::span{file.bytes};
            const auto head = mode == OpenMode::Memory ? bytes : bytes.first(std::min(bytes.size(), kHostHeadSize));
            const auto size = mode == OpenMode::Memory ? INT64{0} : static_cast<INT64>(bytes.size());
            const auto path = fixturePath(name).wstring();
            OpenedImage image;
            if (exports.fileOpenW(path.c_str(), size, reinterpret_cast<const BYTE *>(head.data()),
                                  static_cast<UINT32>(head.size()), &image.info, &image.context) == FALSE) {
                return std::nullopt;
            }
            return image;
        }

        BOOL __stdcall abortWebpAfterDecode(void *, const UINT32 step, UINT32)
        {
            return step == 1 ? FALSE : TRUE;
        }

    } // namespace

    TEST_CASE("encrypted WebP round trips through legacy and wide PVD entries in disk and memory modes")
    {
        constexpr std::array cases{
            WebpCase{"webp_rgb_lossless_2x2.png_", 2, false, true, {255, 0, 0, 255}},
            WebpCase{"webp_rgba_lossless_2x2.png_", 2, true, true, {255, 0, 0, 255}},
            WebpCase{"webp_rgb_lossy_4x4.png_", 4, false, false, {255, 1, 0, 255}},
            WebpCase{"webp_rgba_lossy_4x4.png_", 4, true, false, {89, 160, 227, 128}},
            WebpCase{"webp_metadata_2x2.png_", 2, false, true, {255, 0, 0, 255}},
        };

        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        for (const auto &expected : cases) {
            CAPTURE(expected.name);
            const auto file = readFixture(expected.name);
            for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
                CAPTURE(mode);
                auto legacy = openImage(exports, file, mode);
                auto native = openWebpWide(exports, file, expected.name, mode);
                REQUIRE(legacy.has_value());
                REQUIRE(native.has_value());
                CHECK(std::string{legacy->info.pFormatName} == "RPGMVP");
                const std::string compression = expected.name == "webp_metadata_2x2.png_"
                                                    ? "WebP"
                                                    : (expected.lossless ? "WebP lossless" : "WebP lossy");
                CHECK(std::string{legacy->info.pCompression} == compression);
                CHECK(std::string{native->info.pCompression} == compression);
                CHECK(legacy->info.nPages == 1);
                CHECK(legacy->info.Flags == 0);

                auto bgr = decodePage(exports, legacy->context, 0, nullptr, nullptr);
                auto rgb = decodePage(exports, native->context, 0, nullptr, nullptr);
                REQUIRE(bgr.has_value());
                REQUIRE(rgb.has_value());
                const auto channels = expected.alpha ? 4U : 3U;
                const auto sourceBpp = channels * 8U;
                CHECK(bgr->page.lWidth == expected.side);
                CHECK(bgr->page.lHeight == expected.side);
                CHECK(bgr->page.nBPP == sourceBpp);
                CHECK(bgr->decode.nBPP == sourceBpp);
                CHECK(rgb->decode.nBPP == sourceBpp + 1U);
                CHECK(bgr->decode.lImagePitch == static_cast<INT32>(expected.side * channels));
                CHECK(rgb->decode.lImagePitch == bgr->decode.lImagePitch);
                CHECK(bgr->decode.Flags == (expected.alpha ? UINT32{PVD_IDF_ALPHA} : UINT32{0}));
                CHECK(rgb->decode.Flags == bgr->decode.Flags);
                const auto bgrBytes = bgr->pixels();
                const auto rgbBytes = rgb->pixels();
                REQUIRE(bgrBytes.size() == rgbBytes.size());
                for (std::size_t offset = 0; offset < bgrBytes.size(); offset += channels) {
                    CHECK(bgrBytes[offset] == rgbBytes[offset + 2U]);
                    CHECK(bgrBytes[offset + 1U] == rgbBytes[offset + 1U]);
                    CHECK(bgrBytes[offset + 2U] == rgbBytes[offset]);
                    if (expected.alpha) {
                        CHECK(bgrBytes[offset + 3U] == rgbBytes[offset + 3U]);
                    }
                }
                const auto expectedPixel = std::span{expected.first}.first(channels);
                const std::vector<std::uint8_t> expectedRgb{expectedPixel.begin(), expectedPixel.end()};
                CHECK(rgb->pixel(0, 0) == expectedRgb);
                exports.pageFree(legacy->context, &bgr->decode);
                exports.pageFree(native->context, &rgb->decode);
                exports.fileClose(legacy->context);
                exports.fileClose(native->context);
            }
        }
        exports.exit();
    }

    TEST_CASE("encrypted lossless WebP retains straight alpha and independent outstanding decoded pages")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        constexpr std::string_view name = "webp_rgba_lossless_2x2.png_";
        const auto file = readFixture(name);
        auto image = openWebpWide(exports, file, name, OpenMode::Memory);
        REQUIRE(image.has_value());
        auto first = decodePage(exports, image->context, 0, nullptr, nullptr);
        auto second = decodePage(exports, image->context, 0, nullptr, nullptr);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(first->decode.pImage != second->decode.pImage);
        CHECK(first->pixels() == second->pixels());
        CHECK(first->pixel(1, 0) == std::vector<std::uint8_t>{0, 255, 0, 128});
        CHECK(first->pixel(0, 1) == std::vector<std::uint8_t>{0, 0, 255, 0});
        CHECK(first->pixel(1, 1) == std::vector<std::uint8_t>{17, 34, 51, 64});

        exports.pageFree(image->context, &first->decode);
        CHECK(second->pixel(1, 1) == std::vector<std::uint8_t>{17, 34, 51, 64});
        pvdInfoDecode aborted{};
        CHECK(exports.pageDecode(image->context, 0, &aborted, abortWebpAfterDecode, nullptr) == FALSE);
        CHECK(aborted.pImage == nullptr);
        auto afterAbort = decodePage(exports, image->context, 0, nullptr, nullptr);
        REQUIRE(afterAbort.has_value());
        CHECK(afterAbort->pixels() == second->pixels());
        exports.pageFree(image->context, &second->decode);
        exports.pageFree(image->context, &afterAbort->decode);
        exports.fileClose(image->context);
        exports.exit();
    }

    TEST_CASE("animated encrypted WebP is refused by both PVD file entries")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        constexpr std::string_view name = "webp_animation_2x2.png_";
        const auto file = readFixture(name);
        for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
            CHECK_FALSE(openImage(exports, file, mode).has_value());
            CHECK_FALSE(openWebpWide(exports, file, name, mode).has_value());
        }
        exports.exit();
    }

} // namespace pvdkit::e2e
