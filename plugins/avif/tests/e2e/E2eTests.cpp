#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <latch>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"
#include "PluginHost.hpp"

namespace pvdkit::e2e
{
    namespace
    {

        using Bgr = std::vector<std::uint8_t>;

        struct CallbackLog
        {
            std::vector<std::uint32_t> steps;
            std::vector<std::uint32_t> totals;
            std::uint32_t abortAtCall = 0; // 0 = never abort; n = return FALSE on the n-th call
        };

        BOOL __stdcall logProgress(void *context, const UINT32 step, const UINT32 steps)
        {
            auto &log = *static_cast<CallbackLog *>(context);
            log.steps.push_back(step);
            log.totals.push_back(steps);
            return log.steps.size() == log.abortAtCall ? FALSE : TRUE;
        }

        std::string text(const char *value)
        {
            return value == nullptr ? std::string{"<null>"} : std::string{value};
        }

        bool within(const Bgr &actual, const Bgr &expected, const int tolerance)
        {
            REQUIRE(actual.size() == expected.size());
            for (std::size_t channel = 0; channel < actual.size(); ++channel) {
                if (std::abs(static_cast<int>(actual[channel]) - static_cast<int>(expected[channel])) > tolerance) {
                    return false;
                }
            }
            return true;
        }

        std::vector<std::uint16_t> pixel16(const DecodedPage &page, const std::uint32_t x, const std::uint32_t y)
        {
            const auto bytes = page.pixel(x, y);
            REQUIRE(bytes.size() == 8);
            std::vector<std::uint16_t> samples(4);
            for (std::size_t channel = 0; channel < samples.size(); ++channel) {
                samples[channel] = static_cast<std::uint16_t>(bytes[channel * 2]) |
                                   static_cast<std::uint16_t>(bytes[channel * 2 + 1] << 8U);
            }
            return samples;
        }

        std::uint64_t imageHash(const std::span<const std::byte> pixels) noexcept
        {
            constexpr std::uint64_t kOffsetBasis = 14'695'981'039'346'656'037ULL;
            constexpr std::uint64_t kPrime = 1'099'511'628'211ULL;
            auto hash = kOffsetBasis;
            for (const auto value : pixels) {
                hash ^= std::to_integer<std::uint8_t>(value);
                hash *= kPrime;
            }
            return hash;
        }

        constexpr std::uint32_t outputBitsPerPixel(const avif::tests::FixtureExpectation &fixture)
        {
            const auto shallowBits = fixture.alpha ? 32U : 24U;
            return fixture.presentation || fixture.pageBpp > shallowBits ? 64U : shallowBits;
        }

        void checkDecodeFlags(const DecodedPage &decoded, const bool expectedAlpha,
                              const std::uint8_t hostOrientation = 0)
        {
            const auto alphaFlags = expectedAlpha ? UINT32{PVD_IDF_ALPHA} : UINT32{0};
            const auto orientationFlags = static_cast<UINT32>(hostOrientation) << PVD_IDF_ORIENTATION_SHIFT;
            CHECK(decoded.decode.Flags == (alphaFlags | orientationFlags));
        }

        // Opens `file` in `mode` and decodes every page; the caller closes the context.
        struct OpenedAndDecoded
        {
            OpenedImage image;
            std::vector<DecodedPage> pages;
        };

        OpenedAndDecoded openAndDecodeAll(const PluginExports &exports, const FixtureFile &file, const OpenMode mode)
        {
            auto opened = openImage(exports, file, mode);
            REQUIRE(opened.has_value());
            std::vector<DecodedPage> pages;
            for (std::uint32_t page = 0; page < opened->info.nPages; ++page) {
                auto decoded = decodePage(exports, opened->context, page, nullptr, nullptr);
                REQUIRE(decoded.has_value());
                pages.push_back(*decoded);
            }
            return {*opened, std::move(pages)};
        }

        void freeAndClose(const PluginExports &exports, OpenedAndDecoded &opened)
        {
            for (auto &page : opened.pages) {
                exports.pageFree(opened.image.context, &page.decode);
            }
            exports.fileClose(opened.image.context);
        }

        // What one worker thread saw in its open -> decode -> free -> close round trip. Workers never
        // assert (nor call helpers that do): a failing REQUIRE throws, and an exception leaving a thread
        // terminates the process. They record, and the main thread asserts after the join.
        struct WorkerOutcome
        {
            std::string failure; // empty on success, else which step went wrong
            std::vector<std::byte> pixels;
        };

        WorkerOutcome decodeOnWorker(const PluginExports &exports, const FixtureFile &file)
        {
            WorkerOutcome outcome;
            pvdInfoImage info{};
            void *context = nullptr;
            const auto headSize = static_cast<UINT32>(std::min(file.bytes.size(), kHostHeadSize));
            if (exports.fileOpen(file.utf8Path.c_str(), static_cast<INT64>(file.bytes.size()),
                                 reinterpret_cast<const BYTE *>(file.bytes.data()), headSize, &info,
                                 &context) == FALSE) {
                outcome.failure = "pvdFileOpen answered FALSE";
                return outcome;
            }
            if (context == nullptr) {
                outcome.failure = "pvdFileOpen answered TRUE without a context";
                return outcome;
            }
            DecodedPage page;
            if (exports.pageInfo(context, 0, &page.page) == FALSE) {
                outcome.failure = "pvdPageInfo answered FALSE";
            } else if (exports.pageDecode(context, 0, &page.decode, nullptr, nullptr) == FALSE) {
                outcome.failure = "pvdPageDecode answered FALSE";
            } else if (page.decode.pImage == nullptr ||
                       !isSupportedDecodeLayout(page.page.lWidth, page.decode.nBPP, page.decode.lImagePitch)) {
                outcome.failure = "pvdPageDecode handed out an unusable page";
            } else {
                outcome.pixels = page.pixels();
                exports.pageFree(context, &page.decode);
            }
            exports.fileClose(context);
            return outcome;
        }

    } // namespace

    TEST_CASE("the host driver reports a missing module and a module without the exports")
    {
        const auto missing = PluginLibrary::load(fixturePath("does-not-exist.pvd"));
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().find("LoadLibraryW") != std::string::npos);

        const auto notAPlugin = PluginLibrary::load(L"kernel32.dll");
        REQUIRE_FALSE(notAPlugin.has_value());
        CHECK(notAPlugin.error().find("pvdInit") != std::string::npos);
    }

    TEST_CASE("AVIF.pvd loads, initialises and identifies itself like the host expects")
    {
        const auto loaded = PluginLibrary::load(pluginPath());
        REQUIRE(loaded.has_value());
        const auto &exports = loaded->exports();

        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);

        pvdInfoPlugin info{};
        exports.pluginInfo(&info);
        CHECK(info.Priority == 10);
        CHECK(text(info.pName) == "AVIF");
        CHECK(text(info.pVersion) == "1.2.0");
        const auto comments = text(info.pComments);
        CAPTURE(comments);
        CHECK(comments.find("libavif") != std::string::npos);
        CHECK(comments.find("dav1d") != std::string::npos);
        CHECK(comments.find("libyuv") != std::string::npos);
        CHECK(comments.find("static build") != std::string::npos);
        exports.pluginInfo(nullptr);

        exports.exit();
    }

    TEST_CASE("every accepted fixture decodes identically from disk and from memory")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        for (const auto &expected : avif::tests::kAccepted) {
            CAPTURE(expected.name);
            const auto file = readFixture(expected.name);

            auto disk = openAndDecodeAll(exports, file, OpenMode::Disk);
            auto memory = openAndDecodeAll(exports, file, OpenMode::Memory);

            CHECK(disk.image.info.nPages == expected.pages);
            CHECK(disk.image.info.Flags == (expected.pages > 1 ? UINT32{PVD_IIF_ANIMATED} : UINT32{0}));
            CHECK(text(disk.image.info.pFormatName) == "AVIF");
            CHECK(text(disk.image.info.pCompression) == "AV1");
            CHECK_FALSE(text(disk.image.info.pComments).empty());
            CHECK(memory.image.info.nPages == disk.image.info.nPages);
            CHECK(memory.image.info.Flags == disk.image.info.Flags);
            CHECK(text(memory.image.info.pFormatName) == text(disk.image.info.pFormatName));
            CHECK(text(memory.image.info.pCompression) == text(disk.image.info.pCompression));
            CHECK(text(memory.image.info.pComments) == text(disk.image.info.pComments));

            REQUIRE(disk.pages.size() == expected.pages);
            REQUIRE(memory.pages.size() == expected.pages);
            for (std::uint32_t page = 0; page < expected.pages; ++page) {
                CAPTURE(page);
                const auto &d = disk.pages[page];
                const auto &m = memory.pages[page];
                CHECK(d.page.lWidth == expected.width);
                CHECK(d.page.lHeight == expected.height);
                CHECK(d.page.nBPP == expected.pageBpp);
                CHECK((d.page.lFrameTime > 0) == (expected.pages > 1));
                CHECK(d.decode.nBPP == outputBitsPerPixel(expected));
                CHECK(d.decode.pPalette == nullptr);
                CHECK(d.decode.nColorsUsed == 0);
                checkDecodeFlags(d, expected.alpha, expected.hostOrientation);
                CHECK(d.decode.lImagePitch == static_cast<INT32>(expected.width * d.bytesPerPixel()));

                CHECK(m.page.lWidth == d.page.lWidth);
                CHECK(m.page.lHeight == d.page.lHeight);
                CHECK(m.page.nBPP == d.page.nBPP);
                CHECK(m.page.lFrameTime == d.page.lFrameTime);
                CHECK(m.decode.nBPP == d.decode.nBPP);
                CHECK(m.decode.lImagePitch == d.decode.lImagePitch);
                CHECK(m.decode.Flags == d.decode.Flags);
                checkDecodeFlags(m, expected.alpha, expected.hostOrientation);
                CHECK(m.pixels() == d.pixels());
            }

            // Image-info strings stay valid until pvdFileClose, whatever happened in between.
            CHECK(text(disk.image.info.pFormatName) == "AVIF");
            freeAndClose(exports, disk);
            freeAndClose(exports, memory);
        }
        exports.exit();
    }

    TEST_CASE("EXIF orientation is delegated to PictureView unless an irot transform is present")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        struct Case
        {
            std::string_view name;
            UINT32 hostCode;
            std::uint32_t width;
            std::uint32_t height;
        };
        constexpr std::array cases{
            Case{"kodim03_exif_orientation_6.avif", 7, 768, 512},
            Case{"kodim03_exif_orientation_3.avif", 3, 768, 512},
            Case{"abc_color_irot_alpha_irot_plus_exif6.avif", 0, 256, 512},
        };
        for (const auto &item : cases) {
            CAPTURE(item.name);
            auto opened = openAndDecodeAll(exports, readFixture(item.name), OpenMode::Disk);
            REQUIRE(opened.pages.size() == 1);
            const auto &page = opened.pages.front();
            CHECK(page.page.lWidth == item.width);
            CHECK(page.page.lHeight == item.height);
            CHECK((page.decode.Flags >> PVD_IDF_ORIENTATION_SHIFT) == item.hostCode);
            freeAndClose(exports, opened);
        }
        exports.exit();
    }

    TEST_CASE("a fixture libavif refuses is refused consistently in both modes without crashing")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture(avif::tests::kUnsupported.front());
        CHECK_FALSE(openImage(exports, file, OpenMode::Disk).has_value());
        CHECK_FALSE(openImage(exports, file, OpenMode::Memory).has_value());
        exports.exit();
    }

    TEST_CASE("synthetic fixtures decode to the generator's colours in BGR order")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        SUBCASE("lossless RGB quadrants are exact")
        {
            const auto file = readFixture("quad_rgb_lossless.avif");
            auto opened = openAndDecodeAll(exports, file, OpenMode::Disk);
            const auto &page = opened.pages.at(0);
            CHECK(page.decode.nBPP == 24);
            for (const auto [x, y] : {std::pair{0U, 0U}, std::pair{16U, 16U}, std::pair{31U, 31U}}) {
                CHECK(page.pixel(x, y) == Bgr{0, 0, 255});      // red
                CHECK(page.pixel(x + 32, y) == Bgr{0, 255, 0}); // green
                CHECK(page.pixel(x, y + 32) == Bgr{255, 0, 0}); // blue
                CHECK(page.pixel(x + 32, y + 32) == Bgr{255, 255, 255});
            }
            freeAndClose(exports, opened);
        }

        SUBCASE("lossy 4:2:0 quadrants match the generator's ideal colours within 16")
        {
            // SOURCES.md: libavif+libyuv rounding differs from ffmpeg's swscale by up to ~11 on these
            // saturated colours, so the check is against the ideal colours with tolerance 16.
            const auto file = readFixture("quad_yuv420.avif");
            auto opened = openAndDecodeAll(exports, file, OpenMode::Memory);
            const auto &page = opened.pages.at(0);
            CHECK(within(page.pixel(16, 16), Bgr{0, 0, 255}, 16));
            CHECK(within(page.pixel(48, 16), Bgr{0, 255, 0}, 16));
            CHECK(within(page.pixel(16, 48), Bgr{255, 0, 0}, 16));
            CHECK(within(page.pixel(48, 48), Bgr{255, 255, 255}, 16));
            freeAndClose(exports, opened);
        }

        SUBCASE("alpha bands are exact straight BGRA")
        {
            const auto file = readFixture("alpha_steps.avif");
            auto opened = openAndDecodeAll(exports, file, OpenMode::Disk);
            const auto &page = opened.pages.at(0);
            CHECK(page.decode.nBPP == 32);
            CHECK(page.page.nBPP == 32);
            for (const std::uint32_t y : {0U, 16U, 31U}) {
                CHECK(page.pixel(16, y) == Bgr{255, 255, 255, 0});
                CHECK(page.pixel(48, y) == Bgr{255, 255, 255, 128});
                CHECK(page.pixel(80, y) == Bgr{255, 255, 255, 255});
            }
            freeAndClose(exports, opened);
        }

        SUBCASE("the three-frame animation reports its timing and exact frame colours")
        {
            const auto file = readFixture("anim_3frames.avif");
            auto opened = openAndDecodeAll(exports, file, OpenMode::Disk);
            CHECK(opened.image.info.nPages == 3);
            CHECK(opened.image.info.Flags == PVD_IIF_ANIMATED);
            constexpr std::array<std::uint32_t, 3> durations{100, 200, 300};
            const std::array<Bgr, 3> colours{Bgr{0, 0, 255}, Bgr{0, 255, 0}, Bgr{255, 0, 0}};
            for (std::uint32_t frame = 0; frame < 3; ++frame) {
                CAPTURE(frame);
                const auto &page = opened.pages.at(frame);
                CHECK(page.page.lFrameTime == durations[frame]);
                CHECK(page.pixel(0, 0) == colours[frame]);
                CHECK(page.pixel(32, 32) == colours[frame]);
                CHECK(page.pixel(63, 63) == colours[frame]);
            }
            // Pages of one file are independent buffers: decoding frame 2 did not disturb frame 0.
            CHECK(opened.pages[0].pixel(5, 5) == colours[0]);
            freeAndClose(exports, opened);
        }

        SUBCASE("monochrome 4:0:0 decodes to the full-range grey ramp")
        {
            const auto file = readFixture("gray_400.avif");
            auto opened = openAndDecodeAll(exports, file, OpenMode::Memory);
            const auto &page = opened.pages.at(0);
            CHECK(page.decode.nBPP == 24);
            for (const std::uint32_t x : {0U, 1U, 17U, 32U, 50U, 63U}) {
                const auto grey = static_cast<std::uint8_t>(4 * x);
                CHECK(page.pixel(x, 0) == Bgr{grey, grey, grey});
                CHECK(page.pixel(x, 63) == Bgr{grey, grey, grey});
            }
            freeAndClose(exports, opened);
        }

        SUBCASE("10-bit 4:4:4 reports 30 informational bits and decodes a full-range 16-bit ramp")
        {
            const auto file = readFixture("tenbit_444.avif");
            auto opened = openAndDecodeAll(exports, file, OpenMode::Disk);
            const auto &page = opened.pages.at(0);
            CHECK(page.page.nBPP == 30);
            CHECK(page.decode.nBPP == 64);
            CHECK(page.decode.lImagePitch == 64 * 8);
            for (const std::uint32_t x : {0U, 4U, 5U, 20U, 32U, 45U, 58U, 59U, 63U}) {
                CAPTURE(x);
                const float normalized = std::clamp((16.0F * static_cast<float>(x) - 64.0F) / 876.0F, 0.0F, 1.0F);
                const auto grey = static_cast<std::uint16_t>(std::lround(normalized * 65535.0F));
                CHECK(pixel16(page, x, 0) == std::vector<std::uint16_t>{grey, grey, grey, 0xFFFF});
                CHECK(pixel16(page, x, 63) == std::vector<std::uint16_t>{grey, grey, grey, 0xFFFF});
            }
            freeAndClose(exports, opened);
        }

        exports.exit();
    }

    TEST_CASE("HDR AVIF is presented as 64-bit sRGB through the DLL")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        const auto checkFixture = [&](const std::string_view name, const std::string_view note,
                                      const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> &positions,
                                      const std::array<std::array<std::uint16_t, 4>, 3> &expected,
                                      const std::uint64_t expectedHash) {
            const auto file = readFixture(name);
            auto opened = openAndDecodeAll(exports, file, OpenMode::Disk);
            CHECK(text(opened.image.info.pComments).find(note) != std::string::npos);
            const auto &page = opened.pages.at(0);
            CHECK(page.decode.nBPP == 64);
            const auto actualHash = imageHash(page.pixels());
            CAPTURE(actualHash);
            CHECK(actualHash == expectedHash);
            for (std::size_t index = 0; index < positions.size(); ++index) {
                const auto [x, y] = positions[index];
                CAPTURE(name);
                CAPTURE(x);
                CAPTURE(y);
                const auto pixel = pixel16(page, x, y);
                REQUIRE(pixel.size() == 4);
                for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
                    CAPTURE(channel);
                    CHECK(pixel[channel] == expected[index][channel]);
                }
            }
            freeAndClose(exports, opened);
        };

        // The pixels and hashes are those of the presentation order since Task 27 (the BT.2390
        // EETF on max(R, G, B) in the source primaries, then the matrix): every pinned pixel was
        // re-derived in double precision from libavif's own BGRA64 output of the fixture and
        // agrees with the DLL within one code, and the adapter tests hold every pixel of both
        // fixtures bit for bit against the scalar reference of that order.
        checkFixture("colors_hdr_rec2020.avif", "→ sRGB (BT.2390 tone map from PQ 470 nit)",
                     {{{0, 0}, {100, 100}, {199, 199}}},
                     {{{0, 1'606, 65'535, 65'535}, {8'474, 56'432, 65'535, 65'535}, {65'535, 65'535, 65'535, 65'535}}},
                     9'446'473'485'593'613'650ULL);
        // The x86 hash differs because libavif's x86 YUV->RGB conversion hands the presentation a
        // slightly different 16-bit input, not because the presentation differs: the quantizer is
        // proven exact on both architectures (tests/core/colour/PipelineTests.cpp, the exhaustive
        // diagnostic) and the tabulated tone map agrees with the scalar reference on every pixel
        // of this fixture on both (plugins/avif/tests/adapters/DecoderTests.cpp).
        checkFixture("cosmos1650_yuv444_10bpc_p3pq.avif", "→ sRGB (BT.2390 tone map from PQ 1000 nit)",
                     {{{0, 0}, {512, 214}, {1023, 427}}},
                     {{{48'408, 35'806, 25'488, 65'535}, {2'731, 20'686, 42'766, 65'535}, {0, 50'087, 49'464, 65'535}}},
                     sizeof(std::size_t) == 8 ? 11'592'515'619'216'771'261ULL : 9'812'400'899'338'111'123ULL);
        exports.exit();
    }

    TEST_CASE("rotated fixtures swap their dimensions and the irot twins agree")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        const auto rotated = readFixture("abc_color_irot_alpha_irot.avif");
        const auto twin = readFixture("abc_color_irot_alpha_NOirot.avif");
        auto a = openAndDecodeAll(exports, rotated, OpenMode::Disk);
        auto b = openAndDecodeAll(exports, twin, OpenMode::Memory);
        // Coded 512x256 with one anti-clockwise quarter turn: the host sees 256x512.
        CHECK(a.pages.at(0).page.lWidth == 256);
        CHECK(a.pages.at(0).page.lHeight == 512);
        CHECK(a.pages.at(0).decode.nBPP == 32);
        CHECK(a.pages.at(0).decode.lImagePitch == 256 * 4);
        CHECK(b.pages.at(0).page.lWidth == 256);
        CHECK(b.pages.at(0).page.lHeight == 512);
        CHECK(b.pages.at(0).decode.nBPP == 32);

        // Both files are abc.png with the same rotation; the twin merely lacks the alpha item's irot
        // association, which libavif tolerates (it hands over the same coded planes either way). The
        // rotated output of both is therefore the same picture, pixel for pixel: opaque letters on a
        // translucent background.
        const auto &pa = a.pages.at(0);
        const auto &pb = b.pages.at(0);
        std::size_t opaque = 0;
        std::size_t translucent = 0;
        int worst = 0;
        for (std::uint32_t y = 0; y < 512; y += 3) {
            for (std::uint32_t x = 0; x < 256; x += 3) {
                const auto pixelA = pa.pixel(x, y);
                const auto pixelB = pb.pixel(x, y);
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    worst = std::max(worst,
                                     std::abs(static_cast<int>(pixelA[channel]) - static_cast<int>(pixelB[channel])));
                }
                opaque += pixelA[3] == 255 ? 1 : 0;
                translucent += pixelA[3] < 255 ? 1 : 0;
            }
        }
        CAPTURE(worst);
        CHECK(worst == 0);
        CHECK(opaque > 0);
        CHECK(translucent > 0);
        freeAndClose(exports, a);
        freeAndClose(exports, b);

        const auto clop = readFixture("clop_irot_imor.avif");
        auto c = openAndDecodeAll(exports, clop, OpenMode::Disk);
        CHECK(c.pages.at(0).page.lWidth == 34);
        CHECK(c.pages.at(0).page.lHeight == 12);
        CHECK(c.pages.at(0).page.nBPP == 40);
        CHECK(c.pages.at(0).decode.nBPP == 64);
        CHECK(c.pages.at(0).decode.lImagePitch == 34 * 8);
        CHECK(c.pages.at(0).decode.Flags == PVD_IDF_ALPHA);
        freeAndClose(exports, c);
        exports.exit();
    }

    TEST_CASE("cosmos deep decode timing is reported without enforcing a performance gate")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("cosmos1650_yuv444_10bpc_p3pq.avif");
        auto image = openImage(exports, file, OpenMode::Disk);
        REQUIRE(image.has_value());

        auto warmUp = decodePage(exports, image->context, 0, nullptr, nullptr);
        REQUIRE(warmUp.has_value());
        exports.pageFree(image->context, &warmUp->decode);

        constexpr int kIterations = 5;
        std::uint32_t outputBpp = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
            REQUIRE(page.has_value());
            outputBpp = page->decode.nBPP;
            exports.pageFree(image->context, &page->decode);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        MESSAGE("cosmos pvdPageDecode: nBPP=" << outputBpp << ", mean=" << microseconds / kIterations << " us ("
                                              << kIterations << " iterations after one warm-up)");

        exports.fileClose(image->context);
        exports.exit();
    }

    TEST_CASE("cosmos per-file open and decode timing is reported without enforcing a performance gate")
    {
        // One complete host-level view of the file: pvdFileOpen, pvdPageDecode, pvdPageFree,
        // pvdFileClose. The first iteration in a freshly loaded DLL is reported separately because
        // it carries every one-time initialisation the plugin performs lazily.
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("cosmos1650_yuv444_10bpc_p3pq.avif");

        const auto viewOnce = [&]() {
            const auto start = std::chrono::steady_clock::now();
            auto image = openImage(exports, file, OpenMode::Disk);
            REQUIRE(image.has_value());
            auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
            REQUIRE(page.has_value());
            CHECK(page->decode.nBPP == 64);
            exports.pageFree(image->context, &page->decode);
            exports.fileClose(image->context);
            return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                .count();
        };

        const auto first = viewOnce();
        constexpr int kIterations = 5;
        long long total = 0;
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            total += viewOnce();
        }
        MESSAGE("cosmos per file (open + decode + free + close): first "
                << first << " us, then mean " << total / kIterations << " us over " << kIterations << " further views");
        exports.exit();
    }

    TEST_CASE("the progress callback sees every step and can abort the decode")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("kodim03_yuv420_8bpc.avif");
        auto opened = openImage(exports, file, OpenMode::Disk);
        REQUIRE(opened.has_value());

        CallbackLog counting;
        auto decoded = decodePage(exports, opened->context, 0, logProgress, &counting);
        REQUIRE(decoded.has_value());
        CHECK(counting.steps == std::vector<std::uint32_t>{0, 1, 2});
        CHECK(counting.totals == std::vector<std::uint32_t>{3, 3, 3});
        const auto reference = decoded->pixels();
        exports.pageFree(opened->context, &decoded->decode);

        for (const std::uint32_t abortAt : {1U, 2U, 3U}) {
            CAPTURE(abortAt);
            CallbackLog aborting;
            aborting.abortAtCall = abortAt;
            pvdInfoDecode info{};
            CHECK(exports.pageDecode(opened->context, 0, &info, logProgress, &aborting) == FALSE);
            CHECK(aborting.steps.size() == abortAt);
            // Nothing was handed out, so there is nothing to free; freeing the untouched struct is a no-op.
            CHECK(info.pImage == nullptr);
            exports.pageFree(opened->context, &info);
        }

        // The session is still usable after an abort.
        auto again = decodePage(exports, opened->context, 0, nullptr, nullptr);
        REQUIRE(again.has_value());
        CHECK(again->pixels() == reference);
        exports.pageFree(opened->context, &again->decode);
        exports.fileClose(opened->context);
        exports.exit();
    }

    TEST_CASE("out-of-range pages are refused and a context closes with un-freed pages")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("anim_3frames.avif");
        auto opened = openImage(exports, file, OpenMode::Memory);
        REQUIRE(opened.has_value());
        REQUIRE(opened->info.nPages == 3);

        pvdInfoPage page{};
        CHECK(exports.pageInfo(opened->context, 3, &page) == FALSE);
        CHECK(exports.pageInfo(opened->context, 0xFFFFFFFFU, &page) == FALSE);
        pvdInfoDecode decode{};
        CHECK(exports.pageDecode(opened->context, 3, &decode, nullptr, nullptr) == FALSE);
        CHECK(decode.pImage == nullptr);
        CHECK(exports.pageInfo(opened->context, 0, nullptr) == FALSE);
        CHECK(exports.pageDecode(opened->context, 0, nullptr, nullptr, nullptr) == FALSE);

        // Two decoded pages are deliberately left un-freed; a null decode struct is ignored.
        REQUIRE(decodePage(exports, opened->context, 0, nullptr, nullptr).has_value());
        REQUIRE(decodePage(exports, opened->context, 2, nullptr, nullptr).has_value());
        exports.pageFree(opened->context, nullptr);
        exports.fileClose(opened->context);
        exports.exit();
    }

    TEST_CASE("two files can be open at once in one plugin instance")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto quad = readFixture("quad_rgb_lossless.avif");
        const auto alpha = readFixture("alpha_steps.avif");
        auto first = openAndDecodeAll(exports, quad, OpenMode::Disk);
        auto second = openAndDecodeAll(exports, alpha, OpenMode::Memory);
        CHECK(first.image.context != second.image.context);
        CHECK(first.pages.at(0).pixel(16, 16) == Bgr{0, 0, 255});
        CHECK(second.pages.at(0).pixel(48, 16) == Bgr{255, 255, 255, 128});

        freeAndClose(exports, first);
        // The second session is unaffected by closing the first.
        auto more = decodePage(exports, second.image.context, 0, nullptr, nullptr);
        REQUIRE(more.has_value());
        CHECK(more->pixel(80, 16) == Bgr{255, 255, 255, 255});
        exports.pageFree(second.image.context, &more->decode);
        freeAndClose(exports, second);
        exports.exit();
    }

    TEST_CASE("non-AVIF, garbage, truncated, empty and short inputs are rejected")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        for (const auto name : avif::tests::kRejected) {
            CAPTURE(name);
            const auto file = readFixture(name);
            CHECK_FALSE(openImage(exports, file, OpenMode::Disk).has_value());
            CHECK_FALSE(openImage(exports, file, OpenMode::Memory).has_value());
        }

        const auto valid = readFixture("white_1x1.avif");
        pvdInfoImage info{};
        void *context = nullptr;
        // Empty head, both with the real size on disk and as a whole (empty) file.
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), static_cast<INT64>(valid.bytes.size()), nullptr, 0, &info,
                               &context) == FALSE);
        CHECK(context == nullptr);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, nullptr, 0, &info, &context) == FALSE);
        // A head shorter than the 12 bytes an ftyp box needs.
        const auto *head = reinterpret_cast<const BYTE *>(valid.bytes.data());
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), static_cast<INT64>(valid.bytes.size()), head, 11, &info,
                               &context) == FALSE);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, 11, &info, &context) == FALSE);
        CHECK(context == nullptr);
        // Null host pointers.
        CHECK(exports.fileOpen(nullptr, 0, head, static_cast<UINT32>(valid.bytes.size()), &info, &context) == FALSE);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, static_cast<UINT32>(valid.bytes.size()), nullptr,
                               &context) == FALSE);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, static_cast<UINT32>(valid.bytes.size()), &info,
                               nullptr) == FALSE);
        // A valid head whose file name does not exist on disk.
        const auto missing = readFixture("white_1x1.avif");
        FixtureFile renamed{fixturePath("does-not-exist.avif").string(), missing.bytes};
        CHECK_FALSE(openImage(exports, renamed, OpenMode::Disk).has_value());
        exports.exit();
    }

    TEST_CASE("exports answer FALSE after pvdExit and the plugin can be re-initialised")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("white_1x1.avif");
        exports.exit();

        CHECK_FALSE(openImage(exports, file, OpenMode::Disk).has_value());
        CHECK_FALSE(openImage(exports, file, OpenMode::Memory).has_value());
        pvdInfoPage page{};
        CHECK(exports.pageInfo(nullptr, 0, &page) == FALSE);
        pvdInfoDecode decode{};
        CHECK(exports.pageDecode(nullptr, 0, &decode, nullptr, nullptr) == FALSE);
        exports.pageFree(nullptr, &decode);
        exports.fileClose(nullptr);
        pvdInfoPlugin info{};
        exports.pluginInfo(&info);
        CHECK(info.Priority == 10);
        CHECK(text(info.pName) == "AVIF");
        CHECK(text(info.pVersion) == "1.2.0");
        CHECK(text(info.pComments).empty());
        exports.exit();

        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
        auto reopened = openAndDecodeAll(exports, file, OpenMode::Disk);
        // white_1x1.avif is lossy 4:4:4, so its single white pixel comes back a shade below 255.
        CHECK(within(reopened.pages.at(0).pixel(0, 0), Bgr{255, 255, 255}, 4));
        freeAndClose(exports, reopened);
        exports.exit();
    }

    TEST_CASE("four threads decode the same fixture concurrently with identical results")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("kodim03_yuv420_8bpc.avif");
        auto reference = openAndDecodeAll(exports, file, OpenMode::Disk);
        const auto expected = reference.pages.at(0).pixels();
        freeAndClose(exports, reference);

        constexpr int kThreads = 4;
        std::latch start{kThreads};
        std::array<WorkerOutcome, kThreads> outcomes;
        {
            std::vector<std::jthread> threads;
            threads.reserve(kThreads);
            for (int index = 0; index < kThreads; ++index) {
                threads.emplace_back([&, index] {
                    start.arrive_and_wait();
                    outcomes[static_cast<std::size_t>(index)] = decodeOnWorker(exports, file);
                });
            }
        }
        for (int index = 0; index < kThreads; ++index) {
            CAPTURE(index);
            const auto &outcome = outcomes[static_cast<std::size_t>(index)];
            CHECK_MESSAGE(outcome.failure.empty(), outcome.failure);
            CHECK(outcome.pixels == expected);
        }
        exports.exit();
    }

} // namespace pvdkit::e2e
