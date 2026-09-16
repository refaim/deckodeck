#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <latch>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Imath/ImathBox.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"
#include "PluginHost.hpp"
#include "support/ReferencePipeline.hpp"
#include "support/ReferencePixels.hpp"

namespace pvdkit::e2e
{
    namespace
    {

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

        exr::tests::Rgba16 pixel16(const DecodedPage &page, const std::uint32_t x, const std::uint32_t y)
        {
            const auto bytes = page.pixel(x, y);
            REQUIRE(bytes.size() == 8);
            const auto sample = [&](const std::size_t channel) {
                return static_cast<std::uint16_t>(bytes[channel * 2] | (bytes[channel * 2 + 1] << 8U));
            };
            return {sample(0), sample(1), sample(2), sample(3)};
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
        // assert: a failing REQUIRE throws, and an exception leaving a thread terminates the process.
        struct WorkerOutcome
        {
            std::string failure;
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

        /// A 4K half RGB scene written with the C++ library into %TEMP% for the timing cases: a
        /// smooth photo-like ramp with a little noise, so ZIP and PIZ both have work to do.
        FixtureFile writeTimingInput(const Imf::Compression compression, const std::string_view stem)
        {
            constexpr int kWidth = 3'840;
            constexpr int kHeight = 2'160;
            const auto path =
                std::filesystem::temp_directory_path() / (std::string{"pvdkit-exr-timing-"} + std::string{stem} + "-" +
                                                          std::to_string(GetCurrentProcessId()) + ".exr");
            std::vector<Imf::Rgba> pixels(static_cast<std::size_t>(kWidth) * kHeight);
            std::uint32_t noise = 0x9E37'79B9U;
            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    noise = noise * 1'664'525U + 1'013'904'223U;
                    const float grain = static_cast<float>(noise >> 24U) / 4'096.0F;
                    auto &pixel = pixels[static_cast<std::size_t>(y) * kWidth + x];
                    pixel.r = Imath::half{static_cast<float>(x) / kWidth * 4.0F + grain};
                    pixel.g = Imath::half{static_cast<float>(y) / kHeight * 2.0F + grain};
                    pixel.b = Imath::half{static_cast<float>(x + y) / (kWidth + kHeight) + grain};
                    pixel.a = Imath::half{1.0F};
                }
            }
            Imf::Header header{kWidth, kHeight};
            header.compression() = compression;
            {
                Imf::RgbaOutputFile file{path.string().c_str(), header, Imf::WRITE_RGB, 1};
                file.setFrameBuffer(pixels.data(), 1, static_cast<std::size_t>(kWidth));
                file.writePixels(kHeight);
            }
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

        struct ViewTiming
        {
            long long open = 0;   ///< pvdFileOpen: the plugin reads and encodes every pixel here.
            long long decode = 0; ///< pvdPageDecode: the shared presentation to sRGB.
            long long total = 0;  ///< open + decode + free + close.
        };

        void reportTiming(const PluginExports &exports, const Imf::Compression compression, const std::string_view stem)
        {
            const auto file = writeTimingInput(compression, stem);
            const auto milliseconds = [](const std::chrono::steady_clock::time_point since) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since)
                    .count();
            };
            const auto viewOnce = [&]() {
                ViewTiming timing;
                const auto start = std::chrono::steady_clock::now();
                auto image = openImage(exports, file, OpenMode::Disk);
                REQUIRE(image.has_value());
                timing.open = milliseconds(start);
                const auto decodeStart = std::chrono::steady_clock::now();
                auto page = decodePage(exports, image->context, 0, nullptr, nullptr);
                REQUIRE(page.has_value());
                timing.decode = milliseconds(decodeStart);
                CHECK(page->decode.nBPP == 64);
                exports.pageFree(image->context, &page->decode);
                exports.fileClose(image->context);
                timing.total = milliseconds(start);
                return timing;
            };
            const auto first = viewOnce();
            constexpr int kIterations = 5;
            ViewTiming sum;
            for (int iteration = 0; iteration < kIterations; ++iteration) {
                const auto timing = viewOnce();
                sum.open += timing.open;
                sum.decode += timing.decode;
                sum.total += timing.total;
            }
            MESSAGE("4K half RGB " << stem << " (" << file.bytes.size()
                                   << " bytes) per file (open + decode + free + close): first " << first.total
                                   << " ms (open " << first.open << ", decode " << first.decode << "), then mean "
                                   << sum.total / kIterations << " ms (open " << sum.open / kIterations << ", decode "
                                   << sum.decode / kIterations << ") over " << kIterations << " further views");
            std::filesystem::remove(std::filesystem::path{reinterpret_cast<const char8_t *>(file.utf8Path.c_str())});
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

    TEST_CASE("EXR.pvd loads, initialises and identifies itself like the host expects")
    {
        const auto loaded = PluginLibrary::load(pluginPath());
        REQUIRE(loaded.has_value());
        const auto &exports = loaded->exports();

        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);

        pvdInfoPlugin info{};
        exports.pluginInfo(&info);
        CHECK(info.Priority == 10);
        CHECK(text(info.pName) == "EXR");
        CHECK(text(info.pVersion) == "1.0.0");
        const auto comments = text(info.pComments);
        CAPTURE(comments);
        CHECK(comments.find("OpenEXR 3.4.13") != std::string::npos);
        CHECK(comments.find("Imath") != std::string::npos);
        CHECK(comments.find("libdeflate") != std::string::npos);
        CHECK(comments.find("OpenJPH") != std::string::npos);
        CHECK(comments.find("static build") != std::string::npos);
        exports.pluginInfo(nullptr);

        exports.exit();
    }

    TEST_CASE("every accepted fixture decodes identically from disk and from memory as BGRA64")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        for (const auto &expected : exr::tests::kAccepted) {
            CAPTURE(expected.name);
            const auto file = readFixture(expected.name);

            auto disk = openAndDecodeAll(exports, file, OpenMode::Disk);
            auto memory = openAndDecodeAll(exports, file, OpenMode::Memory);

            CHECK(disk.image.info.nPages == 1);
            CHECK(disk.image.info.Flags == 0);
            CHECK(text(disk.image.info.pFormatName) == "OpenEXR");
            CHECK_FALSE(text(disk.image.info.pCompression).empty());
            CHECK(text(disk.image.info.pComments).find("→ sRGB (") != std::string::npos);
            CHECK(memory.image.info.nPages == disk.image.info.nPages);
            CHECK(text(memory.image.info.pCompression) == text(disk.image.info.pCompression));
            CHECK(text(memory.image.info.pComments) == text(disk.image.info.pComments));

            REQUIRE(disk.pages.size() == 1);
            REQUIRE(memory.pages.size() == 1);
            const auto &d = disk.pages[0];
            const auto &m = memory.pages[0];
            CHECK(d.page.lWidth == expected.width);
            CHECK(d.page.lHeight == expected.height);
            CHECK(d.page.nBPP == expected.pageBpp);
            CHECK(d.page.lFrameTime == 0);
            CHECK(d.decode.nBPP == 64);
            CHECK(d.decode.pPalette == nullptr);
            CHECK(d.decode.nColorsUsed == 0);
            CHECK(d.decode.Flags == (expected.alpha ? UINT32{PVD_IDF_ALPHA} : UINT32{0}));
            CHECK(d.decode.lImagePitch == static_cast<INT32>(expected.width * 8));
            CHECK(m.page.lWidth == d.page.lWidth);
            CHECK(m.page.lHeight == d.page.lHeight);
            CHECK(m.decode.nBPP == d.decode.nBPP);
            CHECK(m.decode.Flags == d.decode.Flags);
            CHECK(m.pixels() == d.pixels());

            CHECK(text(disk.image.info.pFormatName) == "OpenEXR");
            freeAndClose(exports, disk);
            freeAndClose(exports, memory);
        }
        exports.exit();
    }

    TEST_CASE("host pixels match the double-precision reference pipeline at every recorded position")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        for (const auto &fixture : exr::tests::kReferenceFixtures) {
            CAPTURE(fixture.name);
            auto opened = openAndDecodeAll(exports, readFixture(fixture.name), OpenMode::Disk);
            const auto &page = opened.pages.at(0);
            int worst = 0;
            for (const auto &sample : fixture.samples) {
                CAPTURE(sample.x);
                CAPTURE(sample.y);
                const auto expected = exr::tests::expectedHostPixel(sample, fixture);
                const auto actual = pixel16(page, sample.x, sample.y);
                // The shared presentation works in float over a 16-bit LUT; the reference in double
                // from the standards' equations. A handful of 16-bit codes covers their disagreement.
                for (const auto [a, e] : {std::pair{actual.b, expected.b}, std::pair{actual.g, expected.g},
                                          std::pair{actual.r, expected.r}}) {
                    worst = std::max(worst, std::abs(static_cast<int>(a) - static_cast<int>(e)));
                }
                CHECK(actual.a == expected.a);
            }
            CAPTURE(worst);
            CHECK(worst <= 8);
            freeAndClose(exports, opened);
        }
        exports.exit();
    }

    TEST_CASE("whole-image hashes pin the presented output of representative fixtures")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        struct Pinned
        {
            std::string_view name;
            std::string_view note;
            std::uint64_t hash;
        };
        // Recorded on x64 Release (Debug and Release agree: no float transcendental function
        // runs per pixel, the PQ tables and the presentation LUTs are exact); SOURCES.md documents
        // the reference method. The x86 build of the library's chroma reconstruction
        // (ImfRgbaYca, 27-tap sums) lands one red sample of Rec709_YC's 990,640 one code away
        // from the x64 build; the plugin's own path is the same on both, and LuminanceChromaTests
        // holds it within one code of Imf::RgbaInputFile on every pixel of the same architecture.
        constexpr bool k64 = sizeof(std::size_t) == 8;
        constexpr std::array pinned{
            Pinned{"rgb_half_zip.exr", "→ sRGB (BT.2390 tone map from 497 nit)", 12'113'179'102'565'474'970ULL},
            Pinned{"rgba_premultiplied.exr", "→ sRGB (BT.2390 tone map from 497 nit)", 13'438'565'081'922'907'489ULL},
            Pinned{"chroma_ap0.exr", "linear ACES AP0", 14'442'420'165'258'301'884ULL},
            Pinned{"t07.exr", "→ sRGB (SDR range, no highlight compression)", 18'066'542'899'252'817'109ULL},
            Pinned{"Rec709_YC.exr", "half Y+chroma",
                   k64 ? 18'229'812'072'225'433'002ULL : 4'999'148'834'420'483'629ULL},
            Pinned{"Garden.exr", "tiled 128x128", 15'692'666'211'289'079'754ULL},
        };
        for (const auto &item : pinned) {
            CAPTURE(item.name);
            auto opened = openAndDecodeAll(exports, readFixture(item.name), OpenMode::Memory);
            CHECK(text(opened.image.info.pComments).find(item.note) != std::string::npos);
            const auto hash = imageHash(opened.pages.at(0).pixels());
            CAPTURE(hash);
            CHECK(hash == item.hash);
            freeAndClose(exports, opened);
        }
        exports.exit();
    }

    TEST_CASE("the progress callback sees every step and can abort the decode")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("t01.exr");
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
            CHECK(info.pImage == nullptr);
            exports.pageFree(opened->context, &info);
        }

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
        const auto file = readFixture("rgb_half_piz.exr");
        auto opened = openImage(exports, file, OpenMode::Memory);
        REQUIRE(opened.has_value());
        REQUIRE(opened->info.nPages == 1);

        pvdInfoPage page{};
        CHECK(exports.pageInfo(opened->context, 1, &page) == FALSE);
        CHECK(exports.pageInfo(opened->context, 0xFFFFFFFFU, &page) == FALSE);
        pvdInfoDecode decode{};
        CHECK(exports.pageDecode(opened->context, 1, &decode, nullptr, nullptr) == FALSE);
        CHECK(decode.pImage == nullptr);
        CHECK(exports.pageInfo(opened->context, 0, nullptr) == FALSE);
        CHECK(exports.pageDecode(opened->context, 0, nullptr, nullptr, nullptr) == FALSE);

        REQUIRE(decodePage(exports, opened->context, 0, nullptr, nullptr).has_value());
        REQUIRE(decodePage(exports, opened->context, 0, nullptr, nullptr).has_value());
        exports.pageFree(opened->context, nullptr);
        exports.fileClose(opened->context);
        exports.exit();
    }

    TEST_CASE("two files can be open at once in one plugin instance")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        auto first = openAndDecodeAll(exports, readFixture("data_disjoint.exr"), OpenMode::Disk);
        auto second = openAndDecodeAll(exports, readFixture("t09.exr"), OpenMode::Memory);
        CHECK(first.image.context != second.image.context);
        // Transparent black with alpha, opaque black without. Black leaves the shared BT.2390
        // stage at the 0.005-nit display black, sRGB code 42 (the same lift the HLG tests pin).
        CHECK(pixel16(first.pages.at(0), 3, 3).a == 0);
        CHECK(pixel16(second.pages.at(0), 3, 3).a == 65'535);
        CHECK(pixel16(second.pages.at(0), 3, 3).r == 42);
        CHECK(pixel16(second.pages.at(0), 3, 3).g == 42);
        CHECK(pixel16(second.pages.at(0), 3, 3).b == 42);

        freeAndClose(exports, first);
        auto more = decodePage(exports, second.image.context, 0, nullptr, nullptr);
        REQUIRE(more.has_value());
        CHECK(pixel16(*more, 199, 299).a == 65'535);
        exports.pageFree(second.image.context, &more->decode);
        freeAndClose(exports, second);
        exports.exit();
    }

    TEST_CASE("CIE XYZ files present as their Rec.709 twins, and unusable chromaticities as Rec.709")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        // Two presented pictures compared pixel by pixel over the RGB channels, in 16-bit sRGB
        // codes: the worst and the mean absolute difference, and the share of pixels any of whose
        // channels differs by more than 1000 codes (1.5 % of the range).
        struct Difference
        {
            int worst = 0;
            double mean = 0.0;
            double farShare = 0.0;
        };
        const auto compare = [&](const std::string_view left, const std::string_view right) {
            auto a = openAndDecodeAll(exports, readFixture(left), OpenMode::Memory);
            auto b = openAndDecodeAll(exports, readFixture(right), OpenMode::Memory);
            const auto &pageA = a.pages.at(0);
            const auto &pageB = b.pages.at(0);
            REQUIRE(pageA.page.lWidth == pageB.page.lWidth);
            REQUIRE(pageA.page.lHeight == pageB.page.lHeight);
            Difference difference;
            double sum = 0.0;
            std::size_t farPixels = 0;
            for (std::uint32_t y = 0; y < pageA.page.lHeight; ++y) {
                for (std::uint32_t x = 0; x < pageA.page.lWidth; ++x) {
                    const auto p = pixel16(pageA, x, y);
                    const auto q = pixel16(pageB, x, y);
                    int pixelWorst = 0;
                    for (const auto [u, v] : {std::pair{p.b, q.b}, std::pair{p.g, q.g}, std::pair{p.r, q.r}}) {
                        const auto d = std::abs(static_cast<int>(u) - static_cast<int>(v));
                        pixelWorst = std::max(pixelWorst, d);
                        sum += d;
                    }
                    difference.worst = std::max(difference.worst, pixelWorst);
                    farPixels += pixelWorst > 1'000 ? 1 : 0;
                    CHECK(p.a == q.a);
                }
            }
            const auto pixels = static_cast<double>(pageA.page.lWidth) * pageA.page.lHeight;
            difference.mean = sum / (3.0 * pixels);
            difference.farShare = static_cast<double>(farPixels) / pixels;
            freeAndClose(exports, a);
            freeAndClose(exports, b);
            return difference;
        };

        // The corpus pair: the same picture as Rec.709 and as CIE XYZ, both luminance/chroma. The
        // XYZ values pass through XYZ -> BT.709 unadapted; what remains is the half rounding of
        // the XYZ samples and the chroma subsampling, which was applied in a different space: at
        // saturated reds the reconstructed XYZ lands outside the BT.709 gamut and clips, so a
        // few pixels differ by a lot (measured: mean 80 codes, median 33, 0.84 % of the pixels
        // beyond 1000 codes; every one of those a clipped red or blue) while the picture is the
        // same (both files tone-map from the same 291-nit peak).
        const auto corpus = compare("XYZ_YC.exr", "Rec709_YC.exr");
        CAPTURE(corpus.worst);
        CAPTURE(corpus.mean);
        CAPTURE(corpus.farShare);
        CHECK(corpus.mean < 120.0);
        CHECK(corpus.farShare < 0.02);

        // The synthetic RGB pair: the ramp and its XYZ, no subsampling in the way, so only the
        // half rounding of the XYZ samples remains (measured: mean 9 codes, worst 108).
        const auto ramp = compare("chroma_xyz.exr", "rgb_half_zip.exr");
        CAPTURE(ramp.worst);
        CAPTURE(ramp.mean);
        CHECK(ramp.mean < 20.0);
        CHECK(ramp.worst < 200);
        CHECK(ramp.farShare == 0.0);

        // Unusable chromaticities: Rec.709 stands in, so the pixels are exactly the plain files'.
        const auto whiteY0 = compare("rgb_whitey0.exr", "rgb_half_zip.exr");
        CHECK(whiteY0.worst == 0);
        const auto ycWhiteY0 = compare("yc_whitey0.exr", "chroma_extra_channel.exr");
        CHECK(ycWhiteY0.worst == 0);
        const auto collinear = compare("yc_collinear.exr", "chroma_extra_channel.exr");
        CHECK(collinear.worst == 0);
        for (const auto name : {"rgb_whitey0.exr", "yc_whitey0.exr", "yc_collinear.exr"}) {
            CAPTURE(name);
            auto opened = openAndDecodeAll(exports, readFixture(name), OpenMode::Disk);
            CHECK(text(opened.image.info.pComments).find("linear Rec.709 assumed, unusable chromaticities (") !=
                  std::string::npos);
            freeAndClose(exports, opened);
        }
        exports.exit();
    }

    TEST_CASE("no C++ exception is raised inside the DLL for any fixture, accepted or refused")
    {
        // The firewall in the exports would swallow one, so the count is taken before it can: a
        // vectored handler sees every exception at the raise (0xE06D7363 is the MSVC C++ throw),
        // and an expected failure of a file - a chromaticities set the library would throw on,
        // a hostile header, a corrupt chunk - must produce none (AGENTS.md rule 5). The handler
        // is registered *last* (First = 0), behind the handlers the runtime installed before it:
        // under AddressSanitizer the runtime commits its shadow memory on first touch through
        // its own first-registered handler, and an instrumented handler that runs before it, on
        // a thread whose stack shadow is still untouched (the DLL's band workers of a picture of
        // at least 256 Ki pixels, BrightRingsNanInf.exr), faults again inside the fault and
        // brings the process down. A C++ throw still reaches a last-registered vectored handler
        // before any frame-based handler: the throw below proves it in every build.
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        constexpr DWORD kCppException = 0xE06D7363;
        static std::atomic<unsigned> raised;
        raised = 0;
        const auto handler = [](EXCEPTION_POINTERS *pointers) -> LONG {
            if (pointers->ExceptionRecord->ExceptionCode == kCppException) {
                ++raised;
            }
            return EXCEPTION_CONTINUE_SEARCH;
        };
        void *registration = AddVectoredExceptionHandler(0, handler);
        REQUIRE(registration != nullptr);
        bool caught = false;
        try {
            throw std::runtime_error{"counted by the vectored handler"};
        } catch (const std::runtime_error &) {
            caught = true;
        }
        REQUIRE(caught);
        REQUIRE(raised == 1);
        raised = 0;
        for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
            for (const auto &expected : exr::tests::kAccepted) {
                auto opened = openAndDecodeAll(exports, readFixture(expected.name), mode);
                freeAndClose(exports, opened);
            }
            for (const auto name : exr::tests::kRejected) {
                CHECK_FALSE(openImage(exports, readFixture(name), mode).has_value());
            }
        }
        exports.exit();
        REQUIRE(RemoveVectoredExceptionHandler(registration) != 0);
        CHECK(raised == 0);
    }

    TEST_CASE("non-EXR, malformed, unsupported, oversized, empty and short inputs are rejected")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();

        for (const auto name : exr::tests::kRejected) {
            CAPTURE(name);
            const auto file = readFixture(name);
            CHECK_FALSE(openImage(exports, file, OpenMode::Disk).has_value());
            CHECK_FALSE(openImage(exports, file, OpenMode::Memory).has_value());
        }

        const auto valid = readFixture("rgb_half_zip.exr");
        pvdInfoImage info{};
        void *context = nullptr;
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), static_cast<INT64>(valid.bytes.size()), nullptr, 0, &info,
                               &context) == FALSE);
        CHECK(context == nullptr);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, nullptr, 0, &info, &context) == FALSE);
        const auto *head = reinterpret_cast<const BYTE *>(valid.bytes.data());
        // A head shorter than the 16 bytes recognition needs.
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), static_cast<INT64>(valid.bytes.size()), head, 15, &info,
                               &context) == FALSE);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, 15, &info, &context) == FALSE);
        CHECK(context == nullptr);
        CHECK(exports.fileOpen(nullptr, 0, head, static_cast<UINT32>(valid.bytes.size()), &info, &context) == FALSE);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, static_cast<UINT32>(valid.bytes.size()), nullptr,
                               &context) == FALSE);
        CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, static_cast<UINT32>(valid.bytes.size()), &info,
                               nullptr) == FALSE);
        FixtureFile renamed{fixturePath("does-not-exist.exr").string(), valid.bytes};
        CHECK_FALSE(openImage(exports, renamed, OpenMode::Disk).has_value());
        exports.exit();
    }

    TEST_CASE("exports answer FALSE after pvdExit and the plugin can be re-initialised")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("rgb_half_zip.exr");
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
        CHECK(text(info.pName) == "EXR");
        CHECK(text(info.pVersion) == "1.0.0");
        CHECK(text(info.pComments).empty());
        exports.exit();

        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
        auto reopened = openAndDecodeAll(exports, file, OpenMode::Disk);
        CHECK(reopened.pages.at(0).decode.nBPP == 64);
        freeAndClose(exports, reopened);
        exports.exit();
    }

    TEST_CASE("four threads decode the same fixture concurrently with identical results")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("BrightRingsNanInf.exr");
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

    TEST_CASE("4K timing: half RGB ZIP and PIZ through the DLL" * doctest::skip())
    {
        // Skipped under CTest; `exr_e2e_tests --no-skip=true -tc="4K timing*"` reports the numbers
        // the DESIGN.md quotes. The input is written into %TEMP% by the C++ library and removed.
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        reportTiming(exports, Imf::ZIP_COMPRESSION, "zip");
        reportTiming(exports, Imf::PIZ_COMPRESSION, "piz");
        exports.exit();
    }

} // namespace pvdkit::e2e
