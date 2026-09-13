#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "Fakes.hpp"
#include "core/FileSession.hpp"

namespace pvdkit::core
{
    namespace
    {

        using test::DecoderState;

        std::unique_ptr<IDecoder> decoder(const ImageMeta &meta, DecoderState &state)
        {
            return std::make_unique<test::FakeDecoder>(meta, state);
        }

        std::vector<unsigned> pixelIds(const pvd::DecodedPage &page, const std::uint32_t width,
                                       const std::uint32_t bytesPerPixel)
        {
            std::vector<unsigned> result;
            const auto height = static_cast<std::uint32_t>(page.pixels.size() / page.pitchBytes);
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    result.push_back(
                        std::to_integer<unsigned>(page.pixels[static_cast<std::size_t>(y) * page.pitchBytes +
                                                              static_cast<std::size_t>(x) * bytesPerPixel]));
                }
            }
            return result;
        }

        TEST_CASE("FileSession exposes its image information and rejects out-of-range "
                  "pages")
        {
            DecoderState state;
            const auto imageMeta = test::meta(3, 2, false, 8, 2, true);
            const auto info = test::imageInfo(imageMeta);
            FileSession session(nullptr, decoder(imageMeta, state), info, test::options());

            CHECK(session.imageInfo().pageCount == 2);
            CHECK(session.imageInfo().animated);
            CHECK(session.imageInfo().formatName == "Fake format");
            CHECK(session.imageInfo().compression == "Fake compression");
            CHECK(session.imageInfo().comments == "test image");

            const auto pageInfo = session.pageInfo(2);
            REQUIRE_FALSE(pageInfo.has_value());
            CHECK(pageInfo.error().code == ErrorCode::PageOutOfRange);

            const auto decoded = session.decodePage(2, pvd::Progress{});
            REQUIRE_FALSE(decoded.has_value());
            CHECK(decoded.error().code == ErrorCode::PageOutOfRange);
            CHECK(state.timingFrames.empty());
            CHECK(state.decodedFrames.empty());
        }

        TEST_CASE("pageInfo reports still dimensions depth and no frame timing")
        {
            DecoderState state;
            const auto imageMeta = test::meta(3, 2, false, 8);
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

            const auto info = session.pageInfo(0);

            REQUIRE(info.has_value());
            CHECK(info->width == 3);
            CHECK(info->height == 2);
            CHECK(info->bitsPerPixel == 24);
            CHECK(info->frameTimeMs == 0);
            CHECK(state.timingFrames.empty());
        }

        TEST_CASE("pageInfo reports 10-bit RGB and RGBA informational depth")
        {
            for (const bool alpha : {false, true}) {
                DecoderState state;
                const auto imageMeta = test::meta(3, 2, alpha, 10);
                FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

                const auto info = session.pageInfo(0);

                REQUIRE(info.has_value());
                CHECK(info->bitsPerPixel == (alpha ? 40 : 30));
            }
        }

        TEST_CASE("pageInfo reports palette index depth instead of expanded channel depth")
        {
            for (const bool indexed : {false, true}) {
                DecoderState state;
                auto imageMeta = test::meta(3, 2, true, 4);
                imageMeta.indexed = indexed;
                FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

                const auto info = session.pageInfo(0);

                REQUIRE(info.has_value());
                CHECK(info->bitsPerPixel == (indexed ? 4 : 16));
            }
        }

        TEST_CASE("pageInfo passes through animated frame timing and timing errors")
        {
            DecoderState state;
            state.durationMs = 321;
            const auto imageMeta = test::meta(3, 2, false, 8, 3, true);
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

            const auto info = session.pageInfo(2);
            REQUIRE(info.has_value());
            CHECK(info->frameTimeMs == 321);
            CHECK(state.timingFrames == std::vector<std::uint32_t>{2});

            state.timingError = test::error(ErrorCode::DecodeFailed, "timing failed");
            const auto failed = session.pageInfo(1);
            REQUIRE_FALSE(failed.has_value());
            CHECK(failed.error().code == ErrorCode::DecodeFailed);
            CHECK(failed.error().detail == "timing failed");
        }

        TEST_CASE("decodePage emits BGR24 pixels and all progress steps")
        {
            DecoderState state;
            state.iccProfile = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
            const auto imageMeta = test::meta();
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());
            std::vector<std::pair<std::uint32_t, std::uint32_t>> reports;
            const pvd::Progress progress{[&reports](const std::uint32_t step, const std::uint32_t steps) {
                reports.emplace_back(step, steps);
                return true;
            }};

            const auto decoded = session.decodePage(0, progress);

            REQUIRE(decoded.has_value());
            CHECK(decoded->bitsPerPixel == 24);
            CHECK(decoded->pitchBytes == 9);
            CHECK_FALSE(decoded->hasAlpha);
            CHECK(std::ranges::equal(decoded->iccProfile, state.iccProfile));
            CHECK(decoded->pixels.size() == 18);
            CHECK(pixelIds(*decoded, 3, 3) == std::vector<unsigned>{1, 2, 3, 4, 5, 6});
            CHECK(state.decodedFrames == std::vector<std::uint32_t>{0});
            CHECK(state.decodedFormats == std::vector{pvd::PixelFormat::Bgr24});
            CHECK(state.decodedPitches == std::vector<std::uint32_t>{9});
            CHECK(state.decodedSizes == std::vector<std::size_t>{18});
            CHECK(reports == std::vector<std::pair<std::uint32_t, std::uint32_t>>{{0, 3}, {1, 3}, {2, 3}});
            CHECK(session.freePage(decoded->pixels));
        }

        TEST_CASE("decodePage emits BGRA32 pixels when alpha is present")
        {
            DecoderState state;
            const auto imageMeta = test::meta(3, 2, true);
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

            const auto decoded = session.decodePage(0, pvd::Progress{});

            REQUIRE(decoded.has_value());
            CHECK(decoded->bitsPerPixel == 32);
            CHECK(decoded->pitchBytes == 12);
            CHECK(decoded->pixels.size() == 24);
            CHECK(decoded->hasAlpha);
            CHECK(state.decodedFormats == std::vector{pvd::PixelFormat::Bgra32});
            CHECK(session.freePage(decoded->pixels));
        }

        TEST_CASE("decodePage emits BGRA64 for deep sources when deep output is enabled")
        {
            for (const bool alpha : {false, true}) {
                DecoderState state;
                const auto imageMeta = test::meta(3, 2, alpha, 16);
                auto options = test::options();
                options.deepOutput = true;
                FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), options);

                const auto decoded = session.decodePage(0, pvd::Progress{});

                REQUIRE(decoded.has_value());
                CHECK(decoded->bitsPerPixel == 64);
                CHECK(decoded->pitchBytes == 24);
                CHECK(decoded->pixels.size() == 48);
                CHECK(decoded->hasAlpha == alpha);
                CHECK(pixelIds(*decoded, 3, 8) == std::vector<unsigned>{1, 2, 3, 4, 5, 6});
                CHECK(state.decodedFormats == std::vector{pvd::PixelFormat::Bgra64});
                CHECK(state.decodedPitches == std::vector<std::uint32_t>{24});
                CHECK(state.decodedSizes == std::vector<std::size_t>{48});
                CHECK(session.freePage(decoded->pixels));
            }
        }

        TEST_CASE("deep output keeps sources with at most eight bits per sample in their ordinary format")
        {
            DecoderState state;
            const auto imageMeta = test::meta(3, 2, false, 8);
            auto options = test::options();
            options.deepOutput = true;
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), options);

            const auto decoded = session.decodePage(0, pvd::Progress{});

            REQUIRE(decoded.has_value());
            CHECK(decoded->bitsPerPixel == 24);
            CHECK(decoded->pitchBytes == 9);
            CHECK_FALSE(decoded->hasAlpha);
            CHECK(state.decodedFormats == std::vector{pvd::PixelFormat::Bgr24});
            CHECK(session.freePage(decoded->pixels));
        }

        TEST_CASE("decodePage passes through every scripted decoder error")
        {
            constexpr std::array errors{
                ErrorCode::NotRecognised,    ErrorCode::FileOpenFailed,   ErrorCode::ParseFailed,
                ErrorCode::DecodeFailed,     ErrorCode::ConversionFailed, ErrorCode::PageOutOfRange,
                ErrorCode::Aborted,          ErrorCode::TooLarge,         ErrorCode::UnsupportedFeature,
                ErrorCode::InvalidTransform, ErrorCode::Internal};
            for (const auto code : errors) {
                DecoderState state;
                state.decodeError = test::error(code);
                const auto imageMeta = test::meta();
                FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

                const auto decoded = session.decodePage(0, pvd::Progress{});

                REQUIRE_FALSE(decoded.has_value());
                CHECK(decoded.error().code == code);
            }
        }

        TEST_CASE("decodePage aborts at each progress step without retaining a page")
        {
            for (std::uint32_t abortStep = 0; abortStep < 3; ++abortStep) {
                DecoderState state;
                const auto imageMeta = test::meta();
                FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());
                std::vector<std::uint32_t> reports;
                const pvd::Progress progress{[&](const std::uint32_t step, const std::uint32_t steps) {
                    CHECK(steps == 3);
                    reports.push_back(step);
                    return step != abortStep;
                }};

                const auto decoded = session.decodePage(0, progress);

                REQUIRE_FALSE(decoded.has_value());
                CHECK(decoded.error().code == ErrorCode::Aborted);
                CHECK(reports.back() == abortStep);
                CHECK(state.decodedFrames.size() == (abortStep == 0 ? 0 : 1));

                const auto fresh = session.decodePage(0, pvd::Progress{});
                REQUIRE(fresh.has_value());
                CHECK(pixelIds(*fresh, 3, 3) == std::vector<unsigned>{1, 2, 3, 4, 5, 6});
                CHECK(session.freePage(fresh->pixels));
                CHECK_FALSE(session.freePage(fresh->pixels));
            }
        }

        TEST_CASE("decodePage enforces the coded pixel limit before decoding")
        {
            DecoderState state;
            const auto imageMeta = test::meta(3, 2);
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options(5));

            const auto decoded = session.decodePage(0, pvd::Progress{});

            REQUIRE_FALSE(decoded.has_value());
            CHECK(decoded.error().code == ErrorCode::TooLarge);
            CHECK(state.decodedFrames.empty());
        }

        TEST_CASE("FileSession applies transforms to reported dimensions and decoded "
                  "pixels")
        {
            DecoderState state;
            const Transforms transforms{CropRect{1, 0, 2, 2}, 1, MirrorAxis::LeftRight};
            const auto imageMeta = test::meta(3, 2, true, 8, 1, false, transforms);
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

            const auto info = session.pageInfo(0);
            REQUIRE(info.has_value());
            CHECK(info->width == 2);
            CHECK(info->height == 2);

            const auto decoded = session.decodePage(0, pvd::Progress{});
            REQUIRE(decoded.has_value());
            CHECK(decoded->pitchBytes == 8);
            CHECK(pixelIds(*decoded, 2, 4) == std::vector<unsigned>{6, 3, 5, 2});
            CHECK(session.freePage(decoded->pixels));
        }

        TEST_CASE("FileSession propagates invalid transforms")
        {
            DecoderState state;
            const Transforms transforms{CropRect{2, 1, 2, 2}, 0, std::nullopt};
            const auto imageMeta = test::meta(3, 2, false, 8, 1, false, transforms);
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

            const auto decoded = session.decodePage(0, pvd::Progress{});

            REQUIRE_FALSE(decoded.has_value());
            CHECK(decoded.error().code == ErrorCode::InvalidTransform);
        }

        TEST_CASE("FileSession retains two pages and frees them in either order")
        {
            for (const bool reverse : {false, true}) {
                DecoderState state;
                const auto imageMeta = test::meta(3, 2, false, 8, 2, true);
                FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());

                const auto first = session.decodePage(0, pvd::Progress{});
                REQUIRE(first.has_value());
                const std::vector firstPixels(first->pixels.begin(), first->pixels.end());
                CHECK(pixelIds(*first, 3, 3) == std::vector<unsigned>{1, 2, 3, 4, 5, 6});

                const auto second = session.decodePage(1, pvd::Progress{});
                REQUIRE(second.has_value());
                CHECK(first->pixels.data() != second->pixels.data());
                CHECK(pixelIds(*second, 3, 3) == std::vector<unsigned>{101, 102, 103, 104, 105, 106});
                // The first view stays valid and untouched until freePage.
                CHECK(std::vector(first->pixels.begin(), first->pixels.end()) == firstPixels);
                CHECK(pixelIds(*first, 3, 3) == std::vector<unsigned>{1, 2, 3, 4, 5, 6});

                const auto &earlier = reverse ? *second : *first;
                const auto &later = reverse ? *first : *second;
                CHECK(session.freePage(earlier.pixels));
                CHECK(session.freePage(later.pixels));
            }
        }

        TEST_CASE("freePage rejects unknown and already freed views")
        {
            DecoderState state;
            const auto imageMeta = test::meta();
            FileSession session(nullptr, decoder(imageMeta, state), test::imageInfo(imageMeta), test::options());
            const std::array<std::byte, 3> unknown{};
            CHECK_FALSE(session.freePage(unknown));

            const auto decoded = session.decodePage(0, pvd::Progress{});
            REQUIRE(decoded.has_value());
            const auto pixels = decoded->pixels;
            CHECK(session.freePage(pixels));
            CHECK_FALSE(session.freePage(pixels));
        }

        TEST_CASE("FileSession destruction releases file data decoder and outstanding "
                  "pages")
        {
            DecoderState state;
            int dataDestructions = 0;
            const auto imageMeta = test::meta(3, 2, false, 8, 2, true);
            {
                auto fileData = std::make_unique<test::FakeFileData>(std::vector<std::byte>{std::byte{1}, std::byte{2}},
                                                                     dataDestructions);
                FileSession session(std::move(fileData), decoder(imageMeta, state), test::imageInfo(imageMeta),
                                    test::options());
                REQUIRE(session.decodePage(0, pvd::Progress{}).has_value());
                REQUIRE(session.decodePage(1, pvd::Progress{}).has_value());
                CHECK(dataDestructions == 0);
                CHECK(state.destructions == 0);
            }
            CHECK(dataDestructions == 1);
            CHECK(state.destructions == 1);
        }

    } // namespace
} // namespace pvdkit::core
