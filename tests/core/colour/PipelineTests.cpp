#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "core/colour/Pipeline.hpp"
#include "core/colour/Primaries.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour
{
    namespace
    {

        constexpr std::size_t kTimingIterations = 20;

        /// One SrgbOutputTables for the whole test executable, standing in for the instance a
        /// plugin's composition root owns (a plugin builds its own; a test executable is free to
        /// keep one in a function-local static, the guard rule applies to plugin code only).
        const SrgbOutputTables &testTables()
        {
            static const SrgbOutputTables tables;
            return tables;
        }

        /// The exact sRGB output path the presentation replaced: OETF, clamp, scale, lround.
        std::uint16_t exactSrgbCode(const float linear)
        {
            return static_cast<std::uint16_t>(
                std::lround(std::clamp(Transfer::linearToSrgb(linear), 0.0F, 1.0F) * 65'535.0F));
        }

        /// A tightly packed BGRA64 image whose pixel p carries (17p, 37p, 73p, 65535) as little-endian
        /// 16-bit samples: the Task 20 timing pattern, kept so the timing rows stay comparable.
        std::vector<std::byte> patternImage(const std::size_t width, const std::size_t height)
        {
            std::vector<std::byte> image(width * height * 8);
            const auto store = [&image](const std::size_t offset, const std::uint16_t sample) {
                image[offset] = static_cast<std::byte>(sample & 0xffU);
                image[offset + 1] = static_cast<std::byte>(sample >> 8U);
            };
            for (std::size_t pixel = 0; pixel < width * height; ++pixel) {
                store(pixel * 8, static_cast<std::uint16_t>(pixel * 17));
                store(pixel * 8 + 2, static_cast<std::uint16_t>(pixel * 37));
                store(pixel * 8 + 4, static_cast<std::uint16_t>(pixel * 73));
                store(pixel * 8 + 6, 65'535);
            }
            return image;
        }

        double measurePresentation(const std::size_t width, const std::size_t height, const unsigned maxThreads)
        {
            const auto pixelCount = width * height;
            const auto source = patternImage(width, height);
            const auto pitchBytes = static_cast<std::uint32_t>(width * 8);

            const Presentation presentation{Cicp{12, 16, 12, true}, 1'000.0F, testTables()};
            auto working = source;
            presentation.applyImage(working, pitchBytes, static_cast<std::uint32_t>(height), maxThreads);

            std::array<double, kTimingIterations> timings{};
            for (auto &timing : timings) {
                working = source;
                const auto start = std::chrono::steady_clock::now();
                presentation.applyImage(working, pitchBytes, static_cast<std::uint32_t>(height), maxThreads);
                const auto elapsed = std::chrono::steady_clock::now() - start;
                timing = std::chrono::duration<double, std::nano>(elapsed).count() / static_cast<double>(pixelCount);
            }
            std::ranges::sort(timings);
            CHECK(working[6] == source[6]);
            return (timings[kTimingIterations / 2 - 1] + timings[kTimingIterations / 2]) / 2.0;
        }

        // The timing cases below drive the production Presentation::applyImage (the function
        // FileSession::decodePage calls), so the band rows time the real splitter, not a copy.
        TEST_CASE("Presentation release timing: scalar 1024x428" * doctest::skip())
        {
            constexpr std::size_t kWidth = 1'024;
            constexpr std::size_t kHeight = 428;
            const auto median = measurePresentation(kWidth, kHeight, 1);
            MESSAGE(kWidth << "x" << kHeight << ": median " << median << " ns/pixel, "
                           << median * static_cast<double>(kWidth * kHeight) / 1'000'000.0 << " ms ("
                           << kTimingIterations << " iterations after one warm-up)");
        }

        TEST_CASE("Presentation release timing: scalar 4000x3000" * doctest::skip())
        {
            constexpr std::size_t kWidth = 4'000;
            constexpr std::size_t kHeight = 3'000;
            const auto median = measurePresentation(kWidth, kHeight, 1);
            MESSAGE(kWidth << "x" << kHeight << ": median " << median << " ns/pixel, "
                           << median * static_cast<double>(kWidth * kHeight) / 1'000'000.0 << " ms ("
                           << kTimingIterations << " iterations after one warm-up)");
        }

        TEST_CASE("Presentation release timing: four bands 1024x428" * doctest::skip())
        {
            constexpr std::size_t kWidth = 1'024;
            constexpr std::size_t kHeight = 428;
            const auto median = measurePresentation(kWidth, kHeight, 4);
            MESSAGE(kWidth << "x" << kHeight << ": median " << median << " ns/pixel, "
                           << median * static_cast<double>(kWidth * kHeight) / 1'000'000.0 << " ms ("
                           << kTimingIterations << " iterations after one warm-up, four bands)");
        }

        TEST_CASE("Presentation release timing: four bands 4000x3000" * doctest::skip())
        {
            constexpr std::size_t kWidth = 4'000;
            constexpr std::size_t kHeight = 3'000;
            const auto median = measurePresentation(kWidth, kHeight, 4);
            MESSAGE(kWidth << "x" << kHeight << ": median " << median << " ns/pixel, "
                           << median * static_cast<double>(kWidth * kHeight) / 1'000'000.0 << " ms ("
                           << kTimingIterations << " iterations after one warm-up, four bands)");
        }

        TEST_CASE("Presentation release timing: construction" * doctest::skip())
        {
            constexpr std::size_t kRuns = 10;
            std::array<double, kRuns> tableMilliseconds{};
            for (auto &run : tableMilliseconds) {
                const auto start = std::chrono::steady_clock::now();
                const auto tables = std::make_unique<SrgbOutputTables>();
                run = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                CHECK(tables->quantize(1.0F) == 65'535);
            }
            MESSAGE("SrgbOutputTables construction (once per plugin instance, in pvdInit): best "
                    << *std::ranges::min_element(tableMilliseconds) << " ms, mean "
                    << std::accumulate(tableMilliseconds.begin(), tableMilliseconds.end(), 0.0) /
                           static_cast<double>(kRuns)
                    << " ms over " << kRuns << " constructions");

            // The tables are borrowed, so a session's Presentation costs only its transfer LUT;
            // the first construction is no longer special.
            std::array<double, kRuns> milliseconds{};
            for (auto &run : milliseconds) {
                const auto start = std::chrono::steady_clock::now();
                const auto presentation =
                    std::make_unique<Presentation>(Cicp{12, 16, 12, true}, 1'000.0F, testTables());
                run = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                CHECK(presentation->sourcePeakNits() == 1'000.0F);
            }
            MESSAGE("Presentation(P3/PQ 1000 nit) construction over borrowed tables: best "
                    << *std::ranges::min_element(milliseconds) << " ms, mean "
                    << std::accumulate(milliseconds.begin(), milliseconds.end(), 0.0) / static_cast<double>(kRuns)
                    << " ms over " << kRuns << " constructions");
        }

        TEST_CASE("diagnostic: exhaustive quantizer proof over every float in [0, 1]" * doctest::skip())
        {
            // Every one of the 1,065,353,217 floats from 0.0F to 1.0F inclusive (their bit patterns
            // ascend with their values) against the exact OETF-plus-lround path, on four threads.
            // Not a gate because it runs for seconds; run it after any change to the quantizer.
            // The exact path itself is machine-dependent (the UCRT dispatches `pow` per CPU, e.g.
            // FMA3 on x64), so a different machine may derive thresholds that differ by a ULP from
            // this one's; monotonicity of the exact path is the property that keeps the two equal
            // regardless, which is why `totalDecreases` is counted here and asserted below, not
            // just `totalMismatches`.
            constexpr std::uint32_t kLastBits = std::bit_cast<std::uint32_t>(1.0F);
            constexpr unsigned kThreads = 4;
            constexpr std::uint32_t kChunk = (kLastBits + 1) / kThreads;
            const auto &tables = testTables();
            std::array<std::uint64_t, kThreads> mismatches{};
            std::array<std::uint64_t, kThreads> decreases{};
            {
                std::vector<std::jthread> workers;
                workers.reserve(kThreads);
                for (unsigned thread = 0; thread < kThreads; ++thread) {
                    workers.emplace_back([&, thread]() {
                        const auto begin = kChunk * thread;
                        const auto end = thread + 1 == kThreads ? kLastBits + 1 : begin + kChunk;
                        std::uint16_t previous = 0;
                        for (std::uint32_t bits = begin; bits < end; ++bits) {
                            const auto expected = exactSrgbCode(std::bit_cast<float>(bits));
                            mismatches[thread] += tables.quantize(std::bit_cast<float>(bits)) != expected ? 1U : 0U;
                            decreases[thread] += expected < previous ? 1U : 0U;
                            previous = expected;
                        }
                    });
                }
            }
            const auto totalMismatches = std::accumulate(mismatches.begin(), mismatches.end(), std::uint64_t{0});
            const auto totalDecreases = std::accumulate(decreases.begin(), decreases.end(), std::uint64_t{0});
            MESSAGE("exhaustive [0, 1]: " << kLastBits + 1 << " floats, " << totalMismatches << " mismatches, "
                                          << totalDecreases << " exact-path monotonicity violations");
            CHECK(totalMismatches == 0);
            CHECK(totalDecreases == 0);
        }

        TEST_CASE("a Presentation borrows exactly the sRGB output tables it is given, on any thread")
        {
            // There is no process-wide instance any more: a plugin's composition root owns one
            // SrgbOutputTables and every Presentation of that plugin borrows it by reference
            // (docs/ARCHITECTURE.md section 7). Two distinct instances prove that the reference
            // is the injected one and not some hidden shared object; the worker threads prove
            // that a band worker or a session on another thread sees the same borrowed instance.
            const auto first = std::make_unique<const SrgbOutputTables>();
            const auto second = std::make_unique<const SrgbOutputTables>();
            REQUIRE(first.get() != second.get());

            const Presentation p3Pq{Cicp{12, 16, 12, true}, 1'000.0F, *first};
            const Presentation identity{Cicp{1, 13, 6, false}, std::nullopt, *first};
            const Presentation other{Cicp{12, 16, 12, true}, 1'000.0F, *second};
            CHECK(&p3Pq.outputTables() == first.get());
            CHECK(&identity.outputTables() == first.get());
            CHECK(&other.outputTables() == second.get());
            CHECK(&other.outputTables() != &p3Pq.outputTables());

            std::array<bool, 4> sameOnThread{};
            {
                std::vector<std::jthread> workers;
                workers.reserve(sameOnThread.size());
                for (std::size_t index = 0; index < sameOnThread.size(); ++index) {
                    workers.emplace_back([&tables = *first, &flag = sameOnThread[index]]() {
                        const Presentation hlg{Cicp{9, 18, 9, false}, std::nullopt, tables};
                        flag = &hlg.outputTables() == &tables;
                    });
                }
            }
            CHECK(std::ranges::all_of(sameOnThread, [](const bool same) { return same; }));
        }

        TEST_CASE("the shared quantizer answers NaN, non-positive and saturated inputs like the exact path")
        {
            const auto &tables = testTables();
            // lround(NaN) yields LONG_MIN on this CRT, which the exact path narrowed to code 0.
            CHECK(tables.quantize(std::numeric_limits<float>::quiet_NaN()) == 0);
            CHECK(tables.quantize(-0.0F) == 0);
            CHECK(tables.quantize(-1.0F) == 0);
            CHECK(tables.quantize(-std::numeric_limits<float>::infinity()) == 0);
            CHECK(tables.quantize(std::numeric_limits<float>::denorm_min()) ==
                  exactSrgbCode(std::numeric_limits<float>::denorm_min()));
            CHECK(tables.quantize(std::numeric_limits<float>::min()) ==
                  exactSrgbCode(std::numeric_limits<float>::min()));
            CHECK(tables.quantize(std::nextafter(1.0F, 0.0F)) == exactSrgbCode(std::nextafter(1.0F, 0.0F)));
            CHECK(tables.quantize(1.0F) == 65'535);
            CHECK(tables.quantize(2.0F) == 65'535);
            CHECK(tables.quantize(std::numeric_limits<float>::infinity()) == 65'535);
        }

        TEST_CASE("applyImage converts identically on the calling thread and in disjoint row bands")
        {
            const Presentation presentation{Cicp{12, 16, 12, true}, 1'000.0F, testTables()};

            SUBCASE("a large image with an uneven row count")
            {
                constexpr std::size_t kWidth = 512;
                constexpr std::size_t kHeight = 513; // 262,656 pixels: banded; 513 % 4 = 1
                const auto source = patternImage(kWidth, kHeight);
                auto serial = source;
                presentation.apply(serial);
                CHECK(serial != source);

                for (const unsigned maxThreads : {0U, 1U, 2U, 3U, 4U, 8U}) {
                    CAPTURE(maxThreads);
                    auto banded = source;
                    presentation.applyImage(banded, kWidth * 8, kHeight, maxThreads);
                    CHECK(banded == serial);
                }
            }

            SUBCASE("a small image and a single-row image stay on the calling thread")
            {
                const auto small = patternImage(16, 16);
                auto serialSmall = small;
                presentation.apply(serialSmall);
                auto bandedSmall = small;
                presentation.applyImage(bandedSmall, 16 * 8, 16, 8);
                CHECK(bandedSmall == serialSmall);

                constexpr std::size_t kWideWidth = 262'144;
                const auto wide = patternImage(kWideWidth, 1);
                auto serialWide = wide;
                presentation.apply(serialWide);
                auto bandedWide = wide;
                presentation.applyImage(bandedWide, kWideWidth * 8, 1, 8);
                CHECK(bandedWide == serialWide);
            }
        }

        TEST_CASE("Presentation identity rule preserves every sRGB-ish signalling combination")
        {
            for (const auto primaries : std::array<std::uint16_t, 2>{1, 2}) {
                for (const auto transfer : std::array<std::uint16_t, 6>{1, 2, 6, 13, 14, 15}) {
                    CAPTURE(primaries);
                    CAPTURE(transfer);
                    CHECK_FALSE(Presentation::needed(Cicp{primaries, transfer, 9, false}));
                }
            }

            CHECK(Presentation::needed(Cicp{9, 13, 9, false}));
            for (const auto transfer : std::array<std::uint16_t, 5>{4, 5, 8, 16, 18}) {
                CAPTURE(transfer);
                CHECK(Presentation::needed(Cicp{1, transfer, 1, false}));
            }
            CHECK_FALSE(Presentation::needed(Cicp{1, 99, 1, false}));
            CHECK(Presentation::needed(Cicp{99, 13, 1, false}));
        }

        TEST_CASE("an identity Presentation leaves BGRA64 rows byte-identical")
        {
            std::array<std::uint16_t, 8> row{0, 1, 32'768, 12'345, 65'535, 42, 9'999, 54'321};
            const auto before = row;
            const Presentation presentation{Cicp{1, 13, 6, false}, std::nullopt, testTables()};

            presentation.apply(row);

            CHECK(row == before);

            std::array<std::byte, 8> byteRow{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                             std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
            const auto beforeBytes = byteRow;
            presentation.apply(byteRow);
            CHECK(byteRow == beforeBytes);
        }

        TEST_CASE("linear and P3-D65 samples are converted to encoded sRGB in BGRA order")
        {
            SUBCASE("linear transfer")
            {
                std::array<std::uint16_t, 4> row{16'384, 32'768, 65'535, 12'345};
                const Presentation presentation{Cicp{1, 8, 0, true}, std::nullopt, testTables()};
                presentation.apply(row);
                CHECK(row[0] == doctest::Approx(35'199).epsilon(1.0e-4));
                CHECK(row[1] == doctest::Approx(48'192).epsilon(1.0e-4));
                CHECK(row[2] == 65'535);
                CHECK(row[3] == 12'345);
            }

            SUBCASE("P3-D65 primaries")
            {
                // Source linear RGB is (0.5, 0.25, 0.125); the fixed expected samples were
                // independently calculated from the SMPTE EG 432-1 matrix in PrimariesTests.
                std::array<std::uint16_t, 4> row{8'192, 16'384, 32'768, 60'000};
                const Presentation presentation{Cicp{12, 8, 0, true}, std::nullopt, testTables()};
                presentation.apply(row);
                CHECK(row[0] == doctest::Approx(23'727).epsilon(1.0e-4));
                CHECK(row[1] == doctest::Approx(34'510).epsilon(1.0e-4));
                CHECK(row[2] == doctest::Approx(50'544).epsilon(1.0e-4));
                CHECK(row[3] == 60'000);
            }
        }

        TEST_CASE("linear input quantization matches the exact sRGB OETF over all 16-bit samples")
        {
            constexpr std::size_t kSampleCount = 65'536;
            std::vector<std::uint16_t> row(kSampleCount * 4);
            for (std::size_t sample = 0; sample < kSampleCount; ++sample) {
                const auto value = static_cast<std::uint16_t>(sample);
                row[sample * 4] = value;
                row[sample * 4 + 1] = value;
                row[sample * 4 + 2] = value;
                row[sample * 4 + 3] = value;
            }

            const Presentation presentation{Cicp{1, 8, 0, true}, std::nullopt, testTables()};
            presentation.apply(row);

            for (std::size_t sample = 0; sample < kSampleCount; ++sample) {
                CAPTURE(sample);
                const auto expected = exactSrgbCode(static_cast<float>(sample) / 65'535.0F);
                CHECK(row[sample * 4] == expected);
                CHECK(row[sample * 4 + 1] == expected);
                CHECK(row[sample * 4 + 2] == expected);
                CHECK(row[sample * 4 + 3] == sample);
            }
        }

        TEST_CASE("one Presentation safely produces identical bytes on two disjoint thread bands")
        {
            constexpr std::size_t kPixels = 300'000;
            std::vector<std::uint16_t> source(kPixels * 4);
            for (std::size_t sample = 0; sample < source.size(); ++sample) {
                source[sample] = static_cast<std::uint16_t>(sample * 101);
            }

            const Presentation presentation{Cicp{12, 16, 12, true}, 1'000.0F, testTables()};
            auto serial = source;
            presentation.apply(serial);

            auto parallel = source;
            const auto middle = parallel.size() / 2;
            {
                std::jthread first(
                    [&presentation, band = std::span{parallel}.first(middle)]() noexcept { presentation.apply(band); });
                std::jthread second([&presentation, band = std::span{parallel}.subspan(middle)]() noexcept {
                    presentation.apply(band);
                });
            }

            CHECK(parallel == serial);
        }

        TEST_CASE("PQ is tone-mapped with its mastering peak and quantized to sRGB")
        {
            const auto pq100Nits = static_cast<std::uint16_t>(std::lround(0.5080784215F * 65'535.0F));
            std::array<std::uint16_t, 4> row{pq100Nits, pq100Nits, pq100Nits, 44'444};
            const Presentation presentation{Cicp{1, 16, 0, true}, 1'000.0F, testTables()};

            presentation.apply(row);

            CHECK(presentation.sourcePeakNits() == 1'000.0F);
            CHECK(row[0] == doctest::Approx(55'866).epsilon(2.0e-4));
            CHECK(row[1] == row[0]);
            CHECK(row[2] == row[0]);
            CHECK(row[3] == 44'444);
        }

        TEST_CASE("HLG applies its 1000-nit OOTF and ignores mastering metadata")
        {
            std::array<std::uint16_t, 4> row{32'768, 32'768, 32'768, 65'535};
            const Presentation presentation{Cicp{9, 18, 9, false}, 400.0F, testTables()};
            presentation.apply(row);

            CHECK(presentation.sourcePeakNits() == 1'000.0F);
            CHECK(row[0] == row[1]);
            CHECK(row[1] == row[2]);
            CHECK(row[0] > 0);
            CHECK(row[0] < 65'535);

            std::array<std::uint16_t, 4> black{0, 0, 0, 12'345};
            presentation.apply(black);
            CHECK(black == std::array<std::uint16_t, 4>{42, 42, 42, 12'345});
        }

        TEST_CASE("explicit chromaticities select the run-time primaries matrix and make presentation needed")
        {
            // ACES AP1 (ACEScg) is not an H.273 code: the container reports primaries 2 plus the
            // chromaticities, and the presentation must convert through the run-time matrix
            // exactly as a Presentation over the coded set would through its table.
            constexpr Primaries::Chromaticities ap1{
                {0.713F, 0.293F}, {0.165F, 0.830F}, {0.128F, 0.044F}, {0.32168F, 0.33767F}};
            constexpr Primaries::Chromaticities bt2020{
                {0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
            constexpr Cicp unspecifiedSrgb{2, 13, 0, true};
            constexpr Cicp unspecifiedLinear{2, 8, 0, true};
            CHECK_FALSE(Presentation::needed(unspecifiedSrgb, std::nullopt));
            CHECK(Presentation::needed(unspecifiedSrgb, ap1));
            CHECK(Presentation::needed(unspecifiedLinear, std::nullopt));
            CHECK(Presentation::needed(Cicp{2, 16, 0, true}, std::nullopt));

            // Linear AP1 codes (32768, 16384, 8192)/65535 through the AP1-to-sRGB matrix, then the
            // sRGB OETF (the LUT input is the 16-bit code, not an exact 0.5/0.25/0.125).
            std::array<std::uint16_t, 4> row{8'192, 16'384, 32'768, 60'000};
            const Presentation presentation{unspecifiedLinear, std::nullopt, ap1, testTables()};
            presentation.apply(row);
            const auto expected = [](const float linear) {
                return static_cast<std::uint16_t>(
                    std::lround(std::clamp(Transfer::linearToSrgb(linear), 0.0F, 1.0F) * 65'535.0F));
            };
            const auto converted = Primaries::apply(
                Primaries::toSrgb(ap1), {32'768.0F / 65'535.0F, 16'384.0F / 65'535.0F, 8'192.0F / 65'535.0F});
            CHECK(row[0] == expected(converted[2]));
            CHECK(row[1] == expected(converted[1]));
            CHECK(row[2] == expected(converted[0]));
            CHECK(row[3] == 60'000);

            // Chromaticities equal to a coded set convert byte-identically to that code.
            std::array<std::uint16_t, 4> viaCode{8'192, 16'384, 32'768, 1};
            std::array<std::uint16_t, 4> viaChromaticities = viaCode;
            const Presentation coded{Cicp{9, 16, 9, true}, 1'000.0F, testTables()};
            const Presentation explicitSet{Cicp{2, 16, 0, true}, 1'000.0F, bt2020, testTables()};
            coded.apply(viaCode);
            explicitSet.apply(viaChromaticities);
            CHECK(viaCode == viaChromaticities);

            // With explicit chromaticities the coded primaries are ignored, so the identity code
            // still converts through the explicit set.
            std::array<std::uint16_t, 4> identityCode{8'192, 16'384, 32'768, 1};
            const Presentation ignoredCode{Cicp{1, 16, 0, true}, 1'000.0F, bt2020, testTables()};
            ignoredCode.apply(identityCode);
            CHECK(identityCode == viaCode);

            // A set no derivation can use (a white with y = 0) is ignored in favour of the coded
            // primaries: a container validates before handing over, the presentation never divides.
            constexpr Primaries::Chromaticities unusable{
                {0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.0F}};
            CHECK_FALSE(Primaries::isUsable(unusable));
            std::array<std::uint16_t, 4> viaUnusable{8'192, 16'384, 32'768, 1};
            const Presentation ignoredSet{Cicp{9, 16, 9, true}, 1'000.0F, unusable, testTables()};
            ignoredSet.apply(viaUnusable);
            CHECK(viaUnusable == viaCode);
        }

        TEST_CASE("invalid and unavailable PQ mastering peaks use the 1000-nit fallback")
        {
            CHECK(Presentation{Cicp{9, 16, 9, false}, std::nullopt, testTables()}.sourcePeakNits() == 1'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, -1.0F, testTables()}.sourcePeakNits() == 1'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, std::numeric_limits<float>::quiet_NaN(), testTables()}
                      .sourcePeakNits() == 1'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, 20'000.0F, testTables()}.sourcePeakNits() == 10'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, 600.0F, testTables()}.sourcePeakNits() == 600.0F);
        }

        TEST_CASE("PQ transfer and P3-D65 primaries agree with independent zscale SDR-range samples")
        {
            struct ReferenceSample
            {
                Primaries::Rgb encoded;
                Primaries::Rgb linearBt709;
            };
            // cosmos1650_yuv444_10bpc_p3pq.avif was decoded by zscale to encoded P3/PQ GBR float,
            // then independently converted to linear BT.709 with npl=100. Both selected output
            // pixels are wholly within [0, 1], and tone mapping is deliberately absent.
            constexpr std::array samples{
                ReferenceSample{{0.401375532F, 0.292456090F, 0.172915980F},
                                {0.382961124F, 0.0811400041F, 0.00322370953F}},
                ReferenceSample{{0.494066209F, 0.439880162F, 0.320413500F},
                                {0.952770710F, 0.482266605F, 0.0859083757F}},
            };
            constexpr auto tolerance = 2.0F / 255.0F;

            for (const auto &sample : samples) {
                const Primaries::Rgb sourceNits{Transfer::toLinear(16, sample.encoded[0]),
                                                Transfer::toLinear(16, sample.encoded[1]),
                                                Transfer::toLinear(16, sample.encoded[2])};
                auto actual = Primaries::apply(Primaries::toSrgb(12), sourceNits);
                for (auto &channel : actual) {
                    channel /= 100.0F;
                }

                for (std::size_t channel = 0; channel < actual.size(); ++channel) {
                    CAPTURE(channel);
                    CHECK(sample.linearBt709[channel] >= 0.0F);
                    CHECK(sample.linearBt709[channel] <= 1.0F);
                    CHECK(std::abs(actual[channel] - sample.linearBt709[channel]) <= tolerance);
                }
            }
        }

    } // namespace
} // namespace pvdkit::core::colour
