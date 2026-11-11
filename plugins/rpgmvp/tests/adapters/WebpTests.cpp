#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <webp/decode.h>

#include "adapters/webp/Decoder.hpp"
#include "core/Webp.hpp"

namespace
{
    using pvdkit::core::ErrorCode;
    using pvdkit::pvd::PixelFormat;
    using pvdkit::rpgmvp::webp::DecoderFactory;
    constexpr pvdkit::core::DecoderOptions kOptions{1, false, 268'435'456, 32'768};

    std::vector<std::byte> webpFixture(const std::string_view name)
    {
        std::ifstream input{std::filesystem::path{PVDKIT_FIXTURE_DIR} / name, std::ios::binary | std::ios::ate};
        REQUIRE(input);
        const auto size = input.tellg();
        REQUIRE(size > 0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        REQUIRE(input);
        return bytes;
    }
} // namespace

TEST_CASE("WebP adapter maps statuses without concealing allocation failures")
{
    using namespace pvdkit::rpgmvp::webp;
    CHECK(detail::statusResult(VP8_STATUS_OK, ErrorCode::ParseFailed));
    CHECK_THROWS_AS(static_cast<void>(detail::statusResult(VP8_STATUS_OUT_OF_MEMORY, ErrorCode::ParseFailed)),
                    std::bad_alloc);
    const auto invalid = detail::statusResult(VP8_STATUS_INVALID_PARAM, ErrorCode::ParseFailed);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().code == ErrorCode::Internal);
    const auto unsupported = detail::statusResult(VP8_STATUS_UNSUPPORTED_FEATURE, ErrorCode::ParseFailed);
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error().code == ErrorCode::UnsupportedFeature);
    for (const auto status :
         {VP8_STATUS_BITSTREAM_ERROR, VP8_STATUS_SUSPENDED, VP8_STATUS_USER_ABORT, VP8_STATUS_NOT_ENOUGH_DATA}) {
        const auto failed = detail::statusResult(status, ErrorCode::DecodeFailed);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == ErrorCode::DecodeFailed);
    }
    CHECK(detail::prefixResult(VP8_STATUS_SUSPENDED));
    CHECK(detail::prefixResult(VP8_STATUS_OK));
    CHECK_FALSE(detail::prefixResult(VP8_STATUS_BITSTREAM_ERROR));
    CHECK_THROWS_AS(static_cast<void>(detail::requireDecoder(DecoderHandle{})), std::bad_alloc);
    WebPDecoderConfig config{};
    REQUIRE(WebPInitDecoderConfig(&config));
    CHECK(detail::requireDecoder(DecoderHandle{WebPIDecode(nullptr, 0, &config)}) != nullptr);
    CHECK(libraryVersions().find("libwebp 1.6.0") == 0);
}

TEST_CASE("WebP factory recognises encrypted lossless lossy extended alpha and metadata")
{
    DecoderFactory factory;
    for (const auto name : {"webp_rgb_lossless_2x2.png_", "webp_rgba_lossless_2x2.png_", "webp_rgb_lossy_4x4.png_",
                            "webp_rgba_lossy_4x4.png_", "webp_metadata_2x2.png_"}) {
        CAPTURE(name);
        auto bytes = webpFixture(name);
        CHECK(factory.recognises(bytes));
        auto created = factory.create(bytes, kOptions);
        REQUIRE(created);
        const auto &meta = (*created)->meta();
        const bool lossy = std::string_view{name}.find("lossy") != std::string_view::npos;
        const bool alpha = std::string_view{name}.find("rgba") != std::string_view::npos;
        const bool metadata = std::string_view{name}.find("metadata") != std::string_view::npos;
        CHECK(meta.width == (lossy ? 4U : 2U));
        CHECK(meta.height == (lossy ? 4U : 2U));
        CHECK(meta.depth == 8);
        CHECK(meta.chroma == pvdkit::core::ChromaFormat::Yuv444);
        CHECK(meta.hasAlpha == alpha);
        CHECK_FALSE(meta.alphaPremultiplied);
        CHECK(meta.cicp.primaries == 1);
        CHECK(meta.cicp.transfer == 13);
        CHECK(meta.cicp.matrix == 0);
        CHECK(meta.cicp.fullRange);
        CHECK(meta.frameCount == 1);
        CHECK_FALSE(meta.animated);
        CHECK(meta.compression == (metadata ? "WebP" : (lossy ? "WebP lossy" : "WebP lossless")));
        CHECK(meta.hasIcc == metadata);
        CHECK(meta.hasExif == metadata);
        CHECK(meta.hasXmp == metadata);
        CHECK((*created)->frameTiming(0)->durationMs == 0);
        const auto badTiming = (*created)->frameTiming(1);
        REQUIRE_FALSE(badTiming);
        CHECK(badTiming.error().code == ErrorCode::PageOutOfRange);
    }
}

TEST_CASE("WebP decodes straight alpha and exact lossless pixels directly into all supported output layouts")
{
    DecoderFactory factory;
    constexpr std::array rgba{std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
                              std::byte{0},   std::byte{255}, std::byte{0},   std::byte{128},
                              std::byte{0},   std::byte{0},   std::byte{255}, std::byte{0},
                              std::byte{17},  std::byte{34},  std::byte{51},  std::byte{64}};
    for (const auto alpha : {false, true}) {
        auto bytes = webpFixture(alpha ? "webp_rgba_lossless_2x2.png_" : "webp_rgb_lossless_2x2.png_");
        auto created = factory.create(bytes, kOptions);
        REQUIRE(created);
        for (const auto native : {false, true}) {
            const auto format = alpha ? (native ? PixelFormat::Rgba32 : PixelFormat::Bgra32)
                                      : (native ? PixelFormat::Rgb24 : PixelFormat::Bgr24);
            const auto channels = alpha ? 4U : 3U;
            const auto pitch = 2U * channels;
            const auto pixelBytes = std::size_t{4} * channels;
            std::vector<std::byte> pixels(pixelBytes + 4U, std::byte{0xa5});
            REQUIRE((*created)->decodeFrame(0, format, std::span{pixels}.first(pixelBytes), pitch));
            for (std::size_t pixel = 0; pixel < 4; ++pixel) {
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    const auto sourceChannel = !native && channel < 3U ? 2U - channel : channel;
                    const auto expected = alpha
                                              ? rgba[pixel * 4U + sourceChannel]
                                              : (pixel == sourceChannel || pixel == 3U ? std::byte{255} : std::byte{0});
                    CHECK(pixels[pixel * channels + channel] == expected);
                }
            }
            CHECK(std::ranges::all_of(std::span{pixels}.last(4),
                                      [](const auto value) { return value == std::byte{0xa5}; }));
            const auto first = pixels;
            REQUIRE((*created)->decodeFrame(0, format, std::span{pixels}.first(pixelBytes), pitch));
            CHECK(pixels == first);
        }
    }
}

TEST_CASE("WebP decodes lossy alpha and metadata containers with the reference pixels")
{
    DecoderFactory factory;
    for (const auto alpha : {false, true}) {
        auto bytes = webpFixture(alpha ? "webp_rgba_lossy_4x4.png_" : "webp_rgb_lossy_4x4.png_");
        auto created = factory.create(bytes, kOptions);
        REQUIRE(created);
        const auto channels = alpha ? 4U : 3U;
        const auto format = alpha ? PixelFormat::Rgba32 : PixelFormat::Rgb24;
        std::vector<std::byte> pixels(std::size_t{16} * channels);
        REQUIRE((*created)->decodeFrame(0, format, pixels, 4U * channels));
        constexpr std::array opaque{std::byte{255}, std::byte{1}, std::byte{0}, std::byte{255}};
        constexpr std::array transparent{std::byte{89}, std::byte{160}, std::byte{227}, std::byte{128}};
        const auto &reference = alpha ? transparent : opaque;
        for (std::size_t pixel = 0; pixel < 16; ++pixel) {
            CHECK(std::ranges::equal(std::span{pixels}.subspan(pixel * channels, channels),
                                     std::span{reference}.first(channels)));
        }
    }
    auto metadata = webpFixture("webp_metadata_2x2.png_");
    auto created = factory.create(metadata, kOptions);
    REQUIRE(created);
    std::array<std::byte, 12> pixels{};
    REQUIRE((*created)->decodeFrame(0, PixelFormat::Rgb24, pixels, 6));
    CHECK(pixels[0] == std::byte{255});
    CHECK(pixels[4] == std::byte{255});
    CHECK(pixels[8] == std::byte{255});
}

TEST_CASE("WebP factory refuses unrelated corrupt truncated animated and oversized input")
{
    DecoderFactory factory;
    CHECK_FALSE(factory.recognises({}));
    const auto empty = factory.create({}, kOptions);
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == ErrorCode::NotRecognised);
    auto bytes = webpFixture("webp_rgb_lossless_2x2.png_");
    const auto plain = std::span{bytes}.subspan(16);
    CHECK_FALSE(factory.recognises(plain));
    auto corrupt = bytes;
    corrupt[40] = std::byte{0xff};
    const auto bad = factory.create(corrupt, kOptions);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == ErrorCode::ParseFailed);
    const auto tooShort = factory.create(std::span{bytes}.first(41), kOptions);
    REQUIRE_FALSE(tooShort);
    auto animation = webpFixture("webp_animation_2x2.png_");
    const auto animated = factory.create(animation, kOptions);
    REQUIRE_FALSE(animated);
    CHECK(animated.error().code == ErrorCode::UnsupportedFeature);
    auto limited = kOptions;
    limited.maxDimension = 1;
    const auto dimensions = factory.create(bytes, limited);
    REQUIRE_FALSE(dimensions);
    CHECK(dimensions.error().code == ErrorCode::TooLarge);
    limited = kOptions;
    limited.maxPixels = 3;
    const auto pixels = factory.create(bytes, limited);
    REQUIRE_FALSE(pixels);
    CHECK(pixels.error().code == ErrorCode::TooLarge);
    limited.maxDimension = 0;
    limited.maxPixels = 0;
    limited.maxThreads = 2;
    auto unlimited = factory.create(bytes, limited);
    REQUIRE(unlimited);
    std::array<std::byte, 12> destination{};
    CHECK((*unlimited)->decodeFrame(0, PixelFormat::Rgb24, destination, 6));
}

TEST_CASE("WebP validates page layout and reports damaged compressed data without retaining output")
{
    DecoderFactory factory;
    auto bytes = webpFixture("webp_rgba_lossless_2x2.png_");
    auto created = factory.create(bytes, kOptions);
    REQUIRE(created);
    std::array<std::byte, 16> destination{};
    const auto page = (*created)->decodeFrame(1, PixelFormat::Rgba32, destination, 8);
    REQUIRE_FALSE(page);
    CHECK(page.error().code == ErrorCode::PageOutOfRange);
    for (const auto format :
         {PixelFormat::Rgb24, PixelFormat::Bgr24, PixelFormat::Rgb48, PixelFormat::Rgba64, PixelFormat::Bgra64}) {
        const auto wrong = (*created)->decodeFrame(0, format, destination, 8);
        REQUIRE_FALSE(wrong);
        CHECK(wrong.error().code == ErrorCode::Internal);
    }
    CHECK_FALSE((*created)->decodeFrame(0, PixelFormat::Rgba32, destination, 9));
    CHECK_FALSE((*created)->decodeFrame(0, PixelFormat::Rgba32, std::span{destination}.first(15), 8));
    auto opaque = webpFixture("webp_rgb_lossless_2x2.png_");
    auto opaqueDecoder = factory.create(opaque, kOptions);
    REQUIRE(opaqueDecoder);
    CHECK_FALSE((*opaqueDecoder)->decodeFrame(0, PixelFormat::Rgba32, destination, 6));
    std::ranges::fill(std::span{bytes}.subspan(41), std::byte{0});
    const auto damaged = (*created)->decodeFrame(0, PixelFormat::Rgba32, destination, 8);
    REQUIRE_FALSE(damaged);
    CHECK(damaged.error().code == ErrorCode::DecodeFailed);
}
