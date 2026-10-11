#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <spng.h>

#include "adapters/spng/Decoder.hpp"
#include "core/Error.hpp"
#include "core/Format.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"

namespace
{

    using pvdkit::core::ErrorCode;
    using pvdkit::pvd::PixelFormat;
    using pvdkit::rpgmvp::DecoderFactory;

    constexpr PixelFormat kRgb48 = PixelFormat::Rgb48;
    constexpr pvdkit::core::DecoderOptions kOptions{1, false, 268'435'456, 32'768};
    constexpr std::byte kGuard{0xA5};
    constexpr std::size_t kGuardBytes = 8;

    struct PngSpec
    {
        std::uint32_t width = 7;
        std::uint32_t height = 5;
        std::uint8_t depth = 16;
        std::uint8_t color = SPNG_COLOR_TYPE_TRUECOLOR;
        std::uint8_t interlace = SPNG_INTERLACE_NONE;
        bool sbit = false;
        bool transparency = false;
    };

    struct FreeDeleter
    {
        void operator()(void *pointer) const noexcept
        {
            std::free(pointer);
        }
    };

    std::vector<std::byte> encodedRpgmvp(const PngSpec &spec, const std::span<const std::byte> raw)
    {
        const std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)> context{spng_ctx_new(SPNG_CTX_ENCODER),
                                                                          &spng_ctx_free};
        REQUIRE(context != nullptr);
        REQUIRE(spng_set_option(context.get(), SPNG_ENCODE_TO_BUFFER, 1) == SPNG_OK);
        spng_ihdr ihdr{spec.width, spec.height, spec.depth, spec.color, 0, 0, spec.interlace};
        REQUIRE(spng_set_ihdr(context.get(), &ihdr) == SPNG_OK);
        if (spec.color == SPNG_COLOR_TYPE_INDEXED) {
            spng_plte palette{};
            palette.n_entries = 4;
            palette.entries[0] = {0x12, 0x34, 0x56, 0};
            palette.entries[1] = {0x9A, 0xBC, 0xDE, 0};
            palette.entries[2] = {0xFE, 0x87, 0x01, 0};
            palette.entries[3] = {0x00, 0xFF, 0x7F, 0};
            REQUIRE(spng_set_plte(context.get(), &palette) == SPNG_OK);
        }
        if (spec.sbit) {
            spng_sbit significant{12, 11, 13, 15, 16};
            REQUIRE(spng_set_sbit(context.get(), &significant) == SPNG_OK);
        }
        if (spec.transparency) {
            spng_trns transparency{};
            transparency.gray = 0x1234;
            transparency.red = 0x1234;
            transparency.green = 0x5678;
            transparency.blue = 0x9ABC;
            transparency.n_type3_entries = 4;
            transparency.type3_alpha[0] = 0;
            transparency.type3_alpha[1] = 255;
            transparency.type3_alpha[2] = 127;
            transparency.type3_alpha[3] = 255;
            REQUIRE(spng_set_trns(context.get(), &transparency) == SPNG_OK);
        }
        // RAW makes the test's 16-bit input explicitly PNG big-endian on either host architecture.
        REQUIRE(spng_encode_image(context.get(), raw.data(), raw.size(), SPNG_FMT_RAW, SPNG_ENCODE_FINALIZE) ==
                SPNG_OK);
        std::size_t length = 0;
        int error = SPNG_OK;
        const std::unique_ptr<void, FreeDeleter> buffer{spng_get_png_buffer(context.get(), &length, &error)};
        REQUIRE(error == SPNG_OK);
        REQUIRE(buffer != nullptr);
        const std::span png{static_cast<const std::byte *>(buffer.get()), length};
        constexpr std::array header{std::byte{'R'}, std::byte{'P'}, std::byte{'G'}, std::byte{'M'},
                                    std::byte{'V'}, std::byte{0},   std::byte{0},   std::byte{0},
                                    std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'3'},
                                    std::byte{'0'}, std::byte{'1'}, std::byte{0},   std::byte{0}};
        std::vector<std::byte> file(header.begin(), header.end());
        file.insert(file.end(), pvdkit::rpgmvp::kPngHeader.size(), std::byte{0xA5});
        file.insert(file.end(), png.begin() + static_cast<std::ptrdiff_t>(pvdkit::rpgmvp::kPngHeader.size()),
                    png.end());
        return file;
    }

    std::vector<std::uint16_t> deepSamples(const std::size_t pixels, const std::size_t channels)
    {
        constexpr std::array<std::uint16_t, 8> values{0x1234, 0x5678, 0x9ABC, 0x0000, 0xFFFF, 0x0102, 0xFEDC, 0x8001};
        std::vector<std::uint16_t> samples(pixels * channels);
        for (std::size_t index = 0; index < samples.size(); ++index) {
            samples[index] = values[index % values.size()];
        }
        return samples;
    }

    std::vector<std::byte> png16(const std::span<const std::uint16_t> samples)
    {
        std::vector<std::byte> raw;
        raw.reserve(samples.size() * 2U);
        for (const auto sample : samples) {
            raw.push_back(static_cast<std::byte>(sample >> 8U));
            raw.push_back(static_cast<std::byte>(sample & 0xFFU));
        }
        return raw;
    }

    std::vector<std::byte> fixture(const std::string_view name)
    {
        std::ifstream input{std::filesystem::path{PVDKIT_FIXTURE_DIR} / name, std::ios::binary | std::ios::ate};
        REQUIRE(input);
        const auto size = input.tellg();
        REQUIRE(size >= 0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        REQUIRE(input);
        return bytes;
    }

    void checkGuards(const std::span<const std::byte> storage)
    {
        CHECK(std::ranges::all_of(storage.first(kGuardBytes), [](const auto value) { return value == kGuard; }));
        CHECK(std::ranges::all_of(storage.last(kGuardBytes), [](const auto value) { return value == kGuard; }));
    }

} // namespace

TEST_CASE("RGB48 RPGMVP validation accepts opaque sources at every depth with exactly six bytes per pixel")
{
    pvdkit::core::ImageMeta meta{};
    meta.width = 7;
    meta.height = 5;
    for (const auto depth : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}, std::uint8_t{16}}) {
        CAPTURE(depth);
        meta.depth = depth;
        CHECK(pvdkit::rpgmvp::checkDestination(meta, kRgb48, 210, 42));
        CHECK(pvdkit::rpgmvp::checkDestination(meta, kRgb48, 211, 42));
        CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, kRgb48, 209, 42));
        CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, kRgb48, 210, 41));
        CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, kRgb48, 240, 48));
        meta.hasAlpha = true;
        const auto rejected = pvdkit::rpgmvp::checkDestination(meta, kRgb48, 210, 42);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().code == ErrorCode::Internal);
        meta.hasAlpha = false;
    }
    meta.width = 0x8000'0000U;
    CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, kRgb48, 210, 0));
}

TEST_CASE("RGB48 RPGMVP truecolor16 keeps all sample bits and little endian byte order with Adam7 and sBIT")
{
    DecoderFactory factory;
    for (const auto interlace : {std::uint8_t{SPNG_INTERLACE_NONE}, std::uint8_t{SPNG_INTERLACE_ADAM7}}) {
        for (const auto sbit : {false, true}) {
            CAPTURE(interlace);
            CAPTURE(sbit);
            const PngSpec spec{.interlace = interlace, .sbit = sbit};
            const auto samples = deepSamples(std::size_t{spec.width} * spec.height, 3);
            const auto file = encodedRpgmvp(spec, png16(samples));
            auto decoder = factory.create(file, kOptions);
            REQUIRE(decoder.has_value());
            CHECK_FALSE((*decoder)->meta().hasAlpha);
            const auto imageSize = samples.size() * 2U;
            std::vector<std::byte> storage(imageSize + 2U * kGuardBytes, kGuard);
            auto destination = std::span{storage}.subspan(kGuardBytes, imageSize);
            for (int repeat = 0; repeat < 2; ++repeat) {
                REQUIRE((*decoder)->decodeFrame(0, kRgb48, destination, spec.width * 6U));
                for (std::size_t index = 0; index < samples.size(); ++index) {
                    CHECK(destination[index * 2U] == static_cast<std::byte>(samples[index] & 0xFFU));
                    CHECK(destination[index * 2U + 1U] == static_cast<std::byte>(samples[index] >> 8U));
                }
                checkGuards(storage);
            }
        }
    }
}

TEST_CASE("RGB48 RPGMVP gray16 expands backwards in the final buffer without losing precision")
{
    DecoderFactory factory;
    for (const auto side : {std::uint32_t{1}, std::uint32_t{7}}) {
        for (const auto interlace : {std::uint8_t{SPNG_INTERLACE_NONE}, std::uint8_t{SPNG_INTERLACE_ADAM7}}) {
            for (const auto sbit : {false, true}) {
                CAPTURE(side);
                CAPTURE(interlace);
                CAPTURE(sbit);
                const PngSpec spec{.width = side,
                                   .height = side,
                                   .color = SPNG_COLOR_TYPE_GRAYSCALE,
                                   .interlace = interlace,
                                   .sbit = sbit};
                const auto samples = deepSamples(std::size_t{side} * side, 1);
                const auto file = encodedRpgmvp(spec, png16(samples));
                auto decoder = factory.create(file, kOptions);
                REQUIRE(decoder.has_value());
                const auto imageSize = samples.size() * 6U;
                std::vector<std::byte> storage(imageSize + 2U * kGuardBytes, kGuard);
                auto destination = std::span{storage}.subspan(kGuardBytes, imageSize);
                REQUIRE((*decoder)->decodeFrame(0, kRgb48, destination, side * 6U));
                for (std::size_t pixel = 0; pixel < samples.size(); ++pixel) {
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        const auto offset = pixel * 6U + channel * 2U;
                        CHECK(destination[offset] == static_cast<std::byte>(samples[pixel] & 0xFFU));
                        CHECK(destination[offset + 1U] == static_cast<std::byte>(samples[pixel] >> 8U));
                    }
                }
                checkGuards(storage);
            }
        }
    }
}

TEST_CASE("RGB48 RPGMVP explicitly promotes opaque RGB8 and packed gray or palette samples to full range")
{
    constexpr std::array packed{std::byte{0x1B}, std::byte{0xE4}};
    DecoderFactory factory;
    for (const auto color : {std::uint8_t{SPNG_COLOR_TYPE_GRAYSCALE}, std::uint8_t{SPNG_COLOR_TYPE_INDEXED}}) {
        for (const auto interlace : {std::uint8_t{SPNG_INTERLACE_NONE}, std::uint8_t{SPNG_INTERLACE_ADAM7}}) {
            CAPTURE(color);
            CAPTURE(interlace);
            const PngSpec spec{.width = 4, .height = 2, .depth = 2, .color = color, .interlace = interlace};
            const auto file = encodedRpgmvp(spec, packed);
            auto decoder = factory.create(file, kOptions);
            REQUIRE(decoder.has_value());
            std::array<std::byte, 24> shallow{};
            REQUIRE((*decoder)->decodeFrame(0, PixelFormat::Rgb24, shallow, 12));
            std::vector<std::byte> storage(48U + 2U * kGuardBytes, kGuard);
            auto destination = std::span{storage}.subspan(kGuardBytes, 48);
            REQUIRE((*decoder)->decodeFrame(0, kRgb48, destination, 24));
            for (std::size_t sample = 0; sample < shallow.size(); ++sample) {
                CHECK(destination[sample * 2U] == shallow[sample]);
                CHECK(destination[sample * 2U + 1U] == shallow[sample]);
            }
            checkGuards(storage);
        }
    }
    for (const auto name : {"icc_swapped_rb_64x64.rpgmvp", "gray8_16x8.rpgmvp", "gray1_16x8.rpgmvp"}) {
        CAPTURE(name);
        const auto file = fixture(name);
        auto decoder = factory.create(file, kOptions);
        REQUIRE(decoder.has_value());
        const auto &meta = (*decoder)->meta();
        std::vector<std::byte> shallow(std::size_t{meta.width} * meta.height * 3U);
        std::vector<std::byte> deep(shallow.size() * 2U);
        REQUIRE((*decoder)->decodeFrame(0, PixelFormat::Rgb24, shallow, meta.width * 3U));
        REQUIRE((*decoder)->decodeFrame(0, kRgb48, deep, meta.width * 6U));
        for (std::size_t sample = 0; sample < shallow.size(); ++sample) {
            CHECK(deep[sample * 2U] == shallow[sample]);
            CHECK(deep[sample * 2U + 1U] == shallow[sample]);
        }
    }
}

TEST_CASE("RGB48 RPGMVP refuses alpha and tRNS before touching the destination")
{
    DecoderFactory factory;
    for (const auto color : {std::uint8_t{SPNG_COLOR_TYPE_TRUECOLOR}, std::uint8_t{SPNG_COLOR_TYPE_GRAYSCALE}}) {
        const PngSpec spec{.color = color, .transparency = true};
        const auto channels = color == SPNG_COLOR_TYPE_TRUECOLOR ? std::size_t{3} : std::size_t{1};
        const auto samples = deepSamples(std::size_t{spec.width} * spec.height, channels);
        const auto file = encodedRpgmvp(spec, png16(samples));
        auto decoder = factory.create(file, kOptions);
        REQUIRE(decoder.has_value());
        CHECK((*decoder)->meta().hasAlpha);
        std::vector<std::byte> destination(std::size_t{spec.width} * spec.height * 6U, kGuard);
        const auto result = (*decoder)->decodeFrame(0, kRgb48, destination, spec.width * 6U);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::Internal);
        CHECK(std::ranges::all_of(destination, [](const auto value) { return value == kGuard; }));
    }
    for (const auto name : {"rgba16_60x20_par.rpgmvp", "graya8_16x8.rpgmvp", "indexed4_trns_144x192_cursor.rpgmvp"}) {
        const auto file = fixture(name);
        auto decoder = factory.create(file, kOptions);
        REQUIRE(decoder.has_value());
        const auto &meta = (*decoder)->meta();
        std::vector<std::byte> destination(std::size_t{meta.width} * meta.height * 6U, kGuard);
        const auto result = (*decoder)->decodeFrame(0, kRgb48, destination, meta.width * 6U);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::Internal);
        CHECK(std::ranges::all_of(destination, [](const auto value) { return value == kGuard; }));
    }
}

TEST_CASE("RGB48 RPGMVP preserves bounds page and truncated stream errors")
{
    DecoderFactory factory;
    const PngSpec spec{};
    const auto samples = deepSamples(std::size_t{spec.width} * spec.height, 3);
    auto file = encodedRpgmvp(spec, png16(samples));
    auto decoder = factory.create(file, kOptions);
    REQUIRE(decoder.has_value());
    std::vector<std::byte> destination(samples.size() * 2U, kGuard);
    for (const auto pitch : {spec.width * 6U - 1U, spec.width * 6U + 1U}) {
        const auto result = (*decoder)->decodeFrame(0, kRgb48, destination, pitch);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::Internal);
    }
    const auto small =
        (*decoder)->decodeFrame(0, kRgb48, std::span{destination}.first(destination.size() - 1U), spec.width * 6U);
    REQUIRE_FALSE(small.has_value());
    CHECK(small.error().code == ErrorCode::Internal);
    CHECK(std::ranges::all_of(destination, [](const auto value) { return value == kGuard; }));
    const auto missing = (*decoder)->decodeFrame(1, kRgb48, destination, spec.width * 6U);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::PageOutOfRange);

    file.resize(file.size() - 20U);
    auto truncated = factory.create(file, kOptions);
    REQUIRE(truncated.has_value());
    const auto failed = (*truncated)->decodeFrame(0, kRgb48, destination, spec.width * 6U);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::DecodeFailed);
}
