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
#include <string_view>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "PresentationReference.hpp"
#include "core/colour/Pipeline.hpp"
#include "core/colour/Primaries.hpp"
#include "core/colour/ToneMap.hpp"
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

        /// The pixels of `converted` (tightly packed BGRA64 bytes, `Presentation::apply` output of
        /// `source`) that differ from the scalar reference of `source`; alpha must be untouched.
        std::size_t pixelsDifferingFromReference(const tests::PresentationReference &reference,
                                                 const std::span<const std::byte> source,
                                                 const std::span<const std::byte> converted)
        {
            REQUIRE(source.size() == converted.size());
            const auto load = [](const std::span<const std::byte> bytes, const std::size_t offset) {
                return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]) |
                                                  (std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8U));
            };
            std::size_t differing = 0;
            for (std::size_t offset = 0; offset < source.size(); offset += 8) {
                const auto expected =
                    reference.convert(load(source, offset), load(source, offset + 2), load(source, offset + 4));
                const bool same = load(converted, offset) == expected[0] &&
                                  load(converted, offset + 2) == expected[1] &&
                                  load(converted, offset + 4) == expected[2] &&
                                  load(converted, offset + 6) == load(source, offset + 6);
                differing += same ? 0U : 1U;
            }
            return differing;
        }

        /// ACES AP1 as EXR hands it over: PQ over explicit chromaticities, the per-pixel EETF
        /// after the matrix (the path Tasks 16-26 used for everything).
        constexpr Primaries::Chromaticities kAp1{
            {0.713F, 0.293F}, {0.165F, 0.830F}, {0.128F, 0.044F}, {0.32168F, 0.33767F}};

        double measurePresentation(const std::size_t width, const std::size_t height, const unsigned maxThreads,
                                   const std::optional<Primaries::Chromaticities> &chromaticities = std::nullopt)
        {
            const auto pixelCount = width * height;
            const auto source = patternImage(width, height);
            const auto pitchBytes = static_cast<std::uint32_t>(width * 8);

            const Presentation presentation{chromaticities ? Cicp{2, 16, 0, true} : Cicp{12, 16, 12, true}, 1'000.0F,
                                            chromaticities, testTables()};
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

        TEST_CASE("Presentation release timing: explicit chromaticities 1024x428" * doctest::skip())
        {
            constexpr std::size_t kWidth = 1'024;
            constexpr std::size_t kHeight = 428;
            const auto scalar = measurePresentation(kWidth, kHeight, 1, kAp1);
            const auto banded = measurePresentation(kWidth, kHeight, 4, kAp1);
            MESSAGE(kWidth << "x" << kHeight << " over ACES AP1 (per-pixel EETF after the matrix): median " << scalar
                           << " ns/pixel, " << scalar * static_cast<double>(kWidth * kHeight) / 1'000'000.0
                           << " ms scalar; " << banded << " ns/pixel, "
                           << banded * static_cast<double>(kWidth * kHeight) / 1'000'000.0 << " ms four bands ("
                           << kTimingIterations << " iterations after one warm-up)");
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

            // The tables are borrowed, so a session's Presentation costs its transfer LUT and, for
            // PQ over coded primaries, the BT.2390 gain table: 65,536 EETF evaluations on the
            // calling thread (a session starts no thread at construction).
            const auto measureConstruction = [&](const Cicp &cicp, const std::string_view label) {
                std::array<double, kRuns> milliseconds{};
                for (auto &run : milliseconds) {
                    const auto start = std::chrono::steady_clock::now();
                    const auto presentation = std::make_unique<Presentation>(cicp, 1'000.0F, testTables());
                    run = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    CHECK(presentation->sourcePeakNits() == 1'000.0F);
                }
                MESSAGE(label << ": best " << *std::ranges::min_element(milliseconds) << " ms, mean "
                              << std::accumulate(milliseconds.begin(), milliseconds.end(), 0.0) /
                                     static_cast<double>(kRuns)
                              << " ms over " << kRuns << " constructions");
            };
            measureConstruction(Cicp{9, 1, 9, false}, "Presentation(Rec.2020/BT.1886) construction, LUT only");
            measureConstruction(Cicp{12, 16, 12, true}, "Presentation(P3/PQ 1000 nit) construction, LUT + gain table");
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

            SUBCASE("a large image whose rows are not whole 64-pixel blocks")
            {
                // 500 % 64 = 52: every row (and so every band, and the serial reference) ends in a
                // partial block of the block-wise conversion; 265,000 pixels: banded; 530 % 4 = 2.
                constexpr std::size_t kWidth = 500;
                constexpr std::size_t kHeight = 530;
                const auto source = patternImage(kWidth, kHeight);
                auto serial = source;
                presentation.apply(serial);
                CHECK(serial != source);
                const tests::PresentationReference reference{Cicp{12, 16, 12, true}, 1'000.0F, std::nullopt,
                                                             testTables()};
                CHECK(pixelsDifferingFromReference(reference, source, serial) == 0);

                for (const unsigned maxThreads : {2U, 3U, 4U}) {
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

            // Chromaticities equal to a coded set take the run-time path: the matrix derived from
            // them (within a few ULPs of the coded table) and the EETF after it, per pixel, while
            // the coded session tone-maps before the matrix from its gain table. The two orders
            // differ for saturated codes, so the pixels are not expected to agree; each agrees
            // with the scalar reference of its own path (the sweep case below).
            std::array<std::uint16_t, 4> viaCode{8'192, 16'384, 32'768, 1};
            std::array<std::uint16_t, 4> viaChromaticities = viaCode;
            const Presentation coded{Cicp{9, 16, 9, true}, 1'000.0F, testTables()};
            const Presentation explicitSet{Cicp{2, 16, 0, true}, 1'000.0F, bt2020, testTables()};
            CHECK(coded.toneMapGains().size() == 65'536);
            CHECK(explicitSet.toneMapGains().empty());
            coded.apply(viaCode);
            explicitSet.apply(viaChromaticities);
            const tests::PresentationReference codedReference{Cicp{9, 16, 9, true}, 1'000.0F, std::nullopt,
                                                              testTables()};
            const tests::PresentationReference explicitReference{Cicp{2, 16, 0, true}, 1'000.0F, bt2020, testTables()};
            CHECK(codedReference.toneMapsInSource());
            CHECK_FALSE(explicitReference.toneMapsInSource());
            CHECK(std::array<std::uint16_t, 3>{viaCode[0], viaCode[1], viaCode[2]} ==
                  codedReference.convert(8'192, 16'384, 32'768));
            CHECK(std::array<std::uint16_t, 3>{viaChromaticities[0], viaChromaticities[1], viaChromaticities[2]} ==
                  explicitReference.convert(8'192, 16'384, 32'768));

            // With explicit chromaticities the coded primaries are ignored, so the identity code
            // still converts through the explicit set.
            std::array<std::uint16_t, 4> identityCode{8'192, 16'384, 32'768, 1};
            const Presentation ignoredCode{Cicp{1, 16, 0, true}, 1'000.0F, bt2020, testTables()};
            ignoredCode.apply(identityCode);
            CHECK(identityCode == viaChromaticities);

            // A set no derivation can use (a white with y = 0) is ignored in favour of the coded
            // primaries: a container validates before handing over, the presentation never divides.
            // Only a usable set forces the presentation: with sRGB signalling and an unusable set
            // nothing is converted, so a session must not build a Presentation at all.
            constexpr Primaries::Chromaticities unusable{
                {0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.0F}};
            CHECK_FALSE(Primaries::isUsable(unusable));
            CHECK_FALSE(Presentation::needed(unspecifiedSrgb, unusable));
            CHECK(Presentation::needed(Cicp{9, 13, 9, false}, unusable));
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

        TEST_CASE("the tabulated tone map agrees bit for bit with the scalar reference on every code sweep")
        {
            // The gain table is indexed by the maximum channel code, so every code must be swept
            // in every channel position, alone, as grey and with the other two channels lower: the
            // PQ sessions the plugins produce over coded primaries (P3, Rec.2020 and BT.709,
            // mastering peaks from the SDR range to the PQ peak, the 1000-nit fallback) take the
            // table; PQ over explicit chromaticities (ACES AP1 and a Rec.2020 set as EXR hands
            // them over) and HLG take the per-pixel EETF after the matrix. Each path is held to
            // the scalar reference of the same order.
            constexpr Primaries::Chromaticities ap1{
                {0.713F, 0.293F}, {0.165F, 0.830F}, {0.128F, 0.044F}, {0.32168F, 0.33767F}};
            constexpr Primaries::Chromaticities bt2020{
                {0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
            struct Session
            {
                Cicp cicp;
                std::optional<float> masteringPeakNits;
                std::optional<Primaries::Chromaticities> chromaticities;
                bool tabulated;
            };
            const std::array sessions{
                Session{Cicp{12, 16, 12, true}, 1'000.0F, std::nullopt, true},
                Session{Cicp{9, 16, 9, true}, 470.0F, std::nullopt, true},
                Session{Cicp{1, 16, 1, true}, 10'000.0F, std::nullopt, true},
                Session{Cicp{9, 16, 9, false}, std::nullopt, std::nullopt, true},
                Session{Cicp{9, 16, 9, true}, 100.0F, std::nullopt, true},
                Session{Cicp{2, 16, 0, true}, 497.0F, ap1, false},
                Session{Cicp{2, 16, 0, true}, 1'000.0F, bt2020, false},
                Session{Cicp{9, 18, 9, false}, std::nullopt, std::nullopt, false},
            };
            for (const auto &session : sessions) {
                CAPTURE(session.cicp.primaries);
                CAPTURE(session.cicp.transfer);
                const Presentation presentation{session.cicp, session.masteringPeakNits, session.chromaticities,
                                                testTables()};
                const tests::PresentationReference reference{session.cicp, session.masteringPeakNits,
                                                             session.chromaticities, testTables()};
                CHECK(presentation.toneMapGains().empty() != session.tabulated);
                CHECK(reference.toneMapsInSource() == session.tabulated);
                std::size_t differing = 0;
                std::size_t alphaChanged = 0;
                for (std::uint32_t code = 0; code < 65'536; ++code) {
                    const auto c = static_cast<std::uint16_t>(code);
                    const auto half = static_cast<std::uint16_t>(code / 2);
                    const auto quarter = static_cast<std::uint16_t>(code / 4);
                    const std::array<std::array<std::uint16_t, 3>, 7> pixels{{{c, 0, 0},
                                                                              {0, c, 0},
                                                                              {0, 0, c},
                                                                              {c, c, c},
                                                                              {c, half, quarter},
                                                                              {quarter, c, half},
                                                                              {half, quarter, c}}};
                    for (const auto &pixel : pixels) {
                        std::array<std::uint16_t, 4> row{pixel[0], pixel[1], pixel[2], c};
                        presentation.apply(row);
                        const auto expected = reference.convert(pixel[0], pixel[1], pixel[2]);
                        differing += row[0] == expected[0] && row[1] == expected[1] && row[2] == expected[2] ? 0U : 1U;
                        alphaChanged += row[3] == c ? 0U : 1U;
                    }
                }
                CHECK(differing == 0);
                CHECK(alphaChanged == 0);
            }
        }

        TEST_CASE("the tabulated tone map agrees with the scalar reference on every pixel of whole images")
        {
            const Cicp p3Pq{12, 16, 12, true};
            const Presentation presentation{p3Pq, 1'000.0F, testTables()};
            const tests::PresentationReference reference{p3Pq, 1'000.0F, std::nullopt, testTables()};

            SUBCASE("a random image through four bands")
            {
                constexpr std::size_t kWidth = 1'024;
                constexpr std::size_t kHeight = 512; // 512 Ki pixels: banded
                std::vector<std::byte> source(kWidth * kHeight * 8);
                std::uint32_t state = 0x2545'F491U;
                for (auto &value : source) {
                    state = state * 1'664'525U + 1'013'904'223U;
                    value = static_cast<std::byte>(state >> 24U);
                }
                auto converted = source;
                presentation.applyImage(converted, kWidth * 8, kHeight, 4);
                CHECK(pixelsDifferingFromReference(reference, source, converted) == 0);
            }

            SUBCASE("the timing pattern on the calling thread")
            {
                const auto source = patternImage(512, 256);
                auto converted = source;
                presentation.apply(std::span{converted});
                CHECK(pixelsDifferingFromReference(reference, source, converted) == 0);
            }
        }

        TEST_CASE("the tone-map gain table is built for coded PQ sessions only and holds the per-code EETF gain")
        {
            // SDR, wide-gamut-only and HLG sessions have no use for it and must not pay for it,
            // and PQ over explicit chromaticities tone-maps after the matrix (a usable set; an
            // unusable one is ignored and the coded set's table stands).
            constexpr Primaries::Chromaticities ap0{
                {0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}};
            constexpr Primaries::Chromaticities unusable{
                {0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.0F}};
            CHECK(Presentation{Cicp{1, 13, 6, false}, std::nullopt, testTables()}.toneMapGains().empty());
            CHECK(Presentation{Cicp{9, 13, 9, false}, std::nullopt, testTables()}.toneMapGains().empty());
            CHECK(Presentation{Cicp{12, 8, 0, true}, std::nullopt, testTables()}.toneMapGains().empty());
            CHECK(Presentation{Cicp{9, 18, 9, false}, 1'000.0F, testTables()}.toneMapGains().empty());
            CHECK(Presentation{Cicp{2, 16, 0, true}, 600.0F, ap0, testTables()}.toneMapGains().empty());
            CHECK(Presentation{Cicp{9, 16, 9, true}, 600.0F, unusable, testTables()}.toneMapGains().size() == 65'536);

            // Entry c is the EETF gain of the code's own linear value, mapNits(lut[c]) / lut[c];
            // entry 0 is unused (a pixel whose maximum code is 0 is black by definition) and zero.
            const Presentation serial{Cicp{12, 16, 12, true}, 600.0F, testTables()};
            const auto gains = serial.toneMapGains();
            REQUIRE(gains.size() == 65'536);
            CHECK(gains[0] == 0.0F);
            const ToneMap::Eetf eetf{600.0F};
            std::size_t differing = 0;
            for (std::uint32_t code = 1; code < 65'536; ++code) {
                const auto nits = Transfer::toLinear(16, static_cast<float>(code) / 65'535.0F);
                differing += gains[code] == eetf.mapNits(nits) / nits ? 0U : 1U;
            }
            CHECK(differing == 0);

            // A second session over the same signalling builds the same table and the same pixels.
            std::array<std::uint16_t, 8> row{1, 2, 3, 4, 40'000, 50'000, 60'000, 7};
            serial.apply(row);
            const Presentation again{Cicp{12, 16, 12, true}, 600.0F, testTables()};
            CHECK(std::ranges::equal(again.toneMapGains(), gains));
            std::array<std::uint16_t, 8> againRow{1, 2, 3, 4, 40'000, 50'000, 60'000, 7};
            again.apply(againRow);
            CHECK(againRow == row);
        }

        TEST_CASE(
            "PQ transfer and P3/Rec.2020 primaries agree with independent zscale samples in and above the SDR range")
        {
            struct ReferenceSample
            {
                std::uint16_t primaries;
                Primaries::Rgb encoded;
                Primaries::Rgb linearBt709;
                bool aboveSdrRange;
            };
            // cosmos1650_yuv444_10bpc_p3pq.avif (P3/PQ) and colors_hdr_rec2020.avif (Rec.2020/PQ)
            // were decoded by zscale to encoded GBR float, then independently converted to linear
            // BT.709 with npl=100 (ffmpeg -vf zscale, the commands in the Task 16 report; re-run for
            // Task 27). Tone mapping is deliberately absent on both sides: the two cosmos pixels are
            // wholly within [0, 1], the colors pixel is above it in red and green (an HDR-range
            // sample that the tone map would compress), so only the transfer and the matrix are
            // compared, which is what an untouched zscale reference can validate.
            constexpr std::array samples{
                ReferenceSample{12,
                                {0.401375532F, 0.292456090F, 0.172915980F},
                                {0.382961124F, 0.0811400041F, 0.00322370953F},
                                false},
                ReferenceSample{
                    12, {0.494066209F, 0.439880162F, 0.320413500F}, {0.952770710F, 0.482266605F, 0.0859083757F}, false},
                ReferenceSample{
                    9, {0.547426581F, 0.525673628F, 0.334289908F}, {1.73502994F, 1.16382432F, 0.0248015784F}, true},
            };
            constexpr auto tolerance = 2.0F / 255.0F;

            for (const auto &sample : samples) {
                CAPTURE(sample.primaries);
                const Primaries::Rgb sourceNits{Transfer::toLinear(16, sample.encoded[0]),
                                                Transfer::toLinear(16, sample.encoded[1]),
                                                Transfer::toLinear(16, sample.encoded[2])};
                auto actual = Primaries::apply(Primaries::toSrgb(sample.primaries), sourceNits);
                for (auto &channel : actual) {
                    channel /= 100.0F;
                }

                CHECK(std::ranges::any_of(sample.linearBt709, [](const float value) { return value > 1.0F; }) ==
                      sample.aboveSdrRange);
                for (std::size_t channel = 0; channel < actual.size(); ++channel) {
                    CAPTURE(channel);
                    CHECK(sample.linearBt709[channel] >= 0.0F);
                    CHECK(std::abs(actual[channel] - sample.linearBt709[channel]) <= tolerance);
                }
            }
        }

    } // namespace
} // namespace pvdkit::core::colour
