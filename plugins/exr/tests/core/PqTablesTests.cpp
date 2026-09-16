#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include <doctest/doctest.h>

#include "core/Encode.hpp"
#include "core/PqTables.hpp"

namespace pvdkit::exr
{
    namespace
    {

        /// One PqCodeTables for the whole test executable (a function-local static is fine in a test
        /// executable; a plugin owns its instance in the composition root).
        const PqCodeTables &tables()
        {
            static const PqCodeTables instance;
            return instance;
        }

        TEST_CASE("the PQ code tables answer exactly what the pow path answers at the edges")
        {
            constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
            constexpr float kInf = std::numeric_limits<float>::infinity();
            const std::array values{0.0F,
                                    -0.0F,
                                    -1.0F,
                                    -kInf,
                                    kNaN,
                                    kInf,
                                    1.0e-12F,
                                    3.0e-8F,
                                    1.0e-6F,
                                    0.005F,
                                    0.1F,
                                    1.0F,
                                    18.0F,
                                    100.0F,
                                    203.0F,
                                    1'000.0F,
                                    4'000.0F,
                                    9'999.0F,
                                    9'999.99F,
                                    10'000.0F,
                                    10'000.01F,
                                    20'000.0F,
                                    std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::denorm_min(),
                                    std::numeric_limits<float>::min()};
            for (const auto value : values) {
                CAPTURE(value);
                CHECK(tables().encode(value) == pqCode(value));
            }
            CHECK(tables().encode(0.0F) == 0);
            CHECK(tables().encode(10'000.0F) == 65'535);
        }

        TEST_CASE("the PQ code tables agree with the pow path on a million log-uniform and uniform samples")
        {
            std::mt19937 generator{20'260'916};
            std::uniform_real_distribution<float> logUniform{-30.0F, 14.3F};
            std::uniform_real_distribution<float> uniform{0.0F, 10'000.0F};
            for (int sample = 0; sample < 500'000; ++sample) {
                const auto nits = std::exp2(logUniform(generator));
                CAPTURE(nits);
                REQUIRE(tables().encode(nits) == pqCode(nits));
                const auto linear = uniform(generator);
                CAPTURE(linear);
                REQUIRE(tables().encode(linear) == pqCode(linear));
            }
        }

        TEST_CASE("every decision threshold and its predecessor land on neighbouring codes")
        {
            // The threshold of code c is the first float the pow path maps to c; the float below
            // it maps to c - 1, and both agree with the table by construction.
            for (std::uint32_t code = 1; code <= 65'535; code += 7) {
                const auto threshold = tables().threshold(static_cast<std::uint16_t>(code));
                CAPTURE(code);
                CAPTURE(threshold);
                CHECK(pqCode(threshold) == code);
                CHECK(tables().encode(threshold) == code);
                const auto below = std::nextafter(threshold, 0.0F);
                CHECK(pqCode(below) == code - 1);
                CHECK(tables().encode(below) == code - 1);
            }
            CHECK(tables().threshold(0) == 0.0F);
        }

        TEST_CASE("no bucket holds more thresholds than the fixed search window")
        {
            // The encoder counts the thresholds of a fixed window from the bucket's first one with
            // no data-dependent branch; the window must cover the fullest bucket (the top octave,
            // where the curve is flattest, holds 14 codes per bucket).
            CHECK(tables().widestBucket() <= PqCodeTables::kSearchWindow);
            CHECK(tables().widestBucket() == 14);
            // Half a megabyte of tables lives on the heap: a factory holding them (or a test
            // holding a factory) must fit any stack.
            CHECK(sizeof(PqCodeTables) <= 2 * sizeof(void *));
        }

        TEST_CASE("encodePixels through the tables is what the pow path produces")
        {
            const EncodeParams params{100.0F, true, false, {0.2126F, 0.7152F, 0.0722F}, tables()};
            std::vector<float> rgba;
            std::mt19937 generator{7};
            std::uniform_real_distribution<float> value{-0.5F, 120.0F};
            for (int pixel = 0; pixel < 4'096; ++pixel) {
                rgba.push_back(value(generator));
                rgba.push_back(value(generator));
                rgba.push_back(value(generator));
                rgba.push_back(value(generator) / 100.0F);
            }
            std::vector<std::uint16_t> out(rgba.size());
            LuminanceHistogram histogram;
            encodePixels(rgba, params, out, histogram);
            for (std::size_t pixel = 0; pixel < rgba.size() / 4; ++pixel) {
                const float alpha = std::min(std::max(rgba[pixel * 4 + 3], 0.0F), 1.0F);
                const auto straight = [&](const std::size_t channel) {
                    const float nits = std::max(rgba[pixel * 4 + channel], 0.0F) * 100.0F;
                    return alpha > 0.0F ? nits / alpha : nits;
                };
                CAPTURE(pixel);
                CHECK(out[pixel * 4] == pqCode(straight(2)));
                CHECK(out[pixel * 4 + 1] == pqCode(straight(1)));
                CHECK(out[pixel * 4 + 2] == pqCode(straight(0)));
            }
        }

        TEST_CASE("diagnostic: exhaustive PQ table proof over every float in [0, 10000]" * doctest::skip())
        {
            // Every positive float up to 10000 nit (about 1.07 G values): the tables must answer
            // exactly what pqCode answers (the double-precision curve, lround-ed). Minutes, so skipped under CTest;
            // `exr_core_tests --no-skip=true -tc="diagnostic: exhaustive PQ*"` runs it.
            const auto start = std::chrono::steady_clock::now();
            std::uint64_t checked = 0;
            std::uint64_t mismatches = 0;
            float first = 0.0F;
            for (std::uint32_t bits = 0; bits <= std::bit_cast<std::uint32_t>(10'000.0F); ++bits) {
                const auto nits = std::bit_cast<float>(bits);
                if (tables().encode(nits) != pqCode(nits)) {
                    if (mismatches == 0) {
                        first = nits;
                    }
                    ++mismatches;
                }
                ++checked;
            }
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            MESSAGE("exhaustive PQ tables: " << checked << " floats, " << mismatches << " mismatches (first " << first
                                             << "), " << seconds << " s");
            CHECK(mismatches == 0);
        }

        TEST_CASE("PQ tables timing: 8.3 M encodes" * doctest::skip())
        {
            // Log-uniform random nits: a predictable ramp would let the branch predictor learn the
            // search and hide the cost a real picture pays.
            constexpr std::size_t kCount = 3'840 * 2'160;
            std::vector<float> nits(kCount);
            std::mt19937 generator{1};
            std::uniform_real_distribution<float> logUniform{-8.0F, 13.9F};
            for (auto &value : nits) {
                value = std::exp2(logUniform(generator));
            }
            std::uint64_t sum = 0;
            auto start = std::chrono::steady_clock::now();
            for (const auto value : nits) {
                sum += tables().encode(value);
            }
            const auto table =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            start = std::chrono::steady_clock::now();
            std::uint64_t reference = 0;
            for (const auto value : nits) {
                reference += pqCode(value);
            }
            const auto pow =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            CHECK(sum == reference);
            MESSAGE("PQ encode x " << kCount << ": tables " << table << " ms, pow " << pow << " ms");
        }

    } // namespace
} // namespace pvdkit::exr
