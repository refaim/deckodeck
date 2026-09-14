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
#include <spng.h>

#include "adapters/spng/Decoder.hpp"
#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"

namespace
{

    using pvdkit::core::ChromaFormat;
    using pvdkit::core::ErrorCode;
    using pvdkit::pvd::PixelFormat;
    using pvdkit::rpgmvp::DecoderFactory;

    constexpr pvdkit::core::DecoderOptions kOptions{1, false, 268'435'456, 32'768};

    std::uint16_t sample16(const std::span<const std::byte> pixels, const std::size_t sample)
    {
        const auto offset = sample * 2U;
        return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(pixels[offset])) |
               static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(pixels[offset + 1U]) << 8U);
    }

    std::vector<std::byte> fixture(const std::string_view name)
    {
        const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        REQUIRE(input);
        const auto size = input.tellg();
        REQUIRE(size >= 0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        REQUIRE(input);
        return bytes;
    }

    struct MetaCase
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
        std::uint8_t depth;
        ChromaFormat chroma;
        bool alpha;
        bool indexed;
        bool interlaced;
        bool srgb;
    };

} // namespace

TEST_CASE("libspng result categories and context ownership are stable")
{
    using pvdkit::rpgmvp::ContextHandle;
    using pvdkit::rpgmvp::errorCodeForResult;
    using pvdkit::rpgmvp::requireContext;

    CHECK(errorCodeForResult(SPNG_EUSER_WIDTH) == ErrorCode::TooLarge);
    CHECK(errorCodeForResult(SPNG_EUSER_HEIGHT) == ErrorCode::TooLarge);
    CHECK(errorCodeForResult(SPNG_EOVERFLOW) == ErrorCode::TooLarge);
    CHECK(errorCodeForResult(SPNG_ECHUNK_LIMITS) == ErrorCode::TooLarge);
    CHECK(errorCodeForResult(SPNG_EWIDTH) == ErrorCode::ParseFailed);
    CHECK(pvdkit::rpgmvp::detail::chunkPresent(SPNG_OK) == true);
    CHECK(pvdkit::rpgmvp::detail::chunkPresent(SPNG_ECHUNKAVAIL) == false);
    auto badChunk = pvdkit::rpgmvp::detail::chunkPresent(SPNG_ECHUNK_CRC);
    REQUIRE_FALSE(badChunk.has_value());
    CHECK(badChunk.error().code == ErrorCode::ParseFailed);
    const auto chunkError = std::unexpected(badChunk.error());
    CHECK(pvdkit::rpgmvp::detail::combineChunkPresence(true, false) == std::pair{true, false});
    CHECK_FALSE(pvdkit::rpgmvp::detail::combineChunkPresence(chunkError, false).has_value());
    CHECK_FALSE(pvdkit::rpgmvp::detail::combineChunkPresence(true, chunkError).has_value());
    CHECK(pvdkit::rpgmvp::detail::decodeResult(SPNG_OK));
    auto decodeFailure = pvdkit::rpgmvp::detail::decodeResult(SPNG_EZLIB);
    REQUIRE_FALSE(decodeFailure.has_value());
    CHECK(decodeFailure.error().code == ErrorCode::DecodeFailed);
    auto decodeLimit = pvdkit::rpgmvp::detail::decodeResult(SPNG_EOVERFLOW);
    REQUIRE_FALSE(decodeLimit.has_value());
    CHECK(decodeLimit.error().code == ErrorCode::TooLarge);
    CHECK_THROWS_AS(static_cast<void>(requireContext(ContextHandle{})), std::bad_alloc);
    CHECK(requireContext(ContextHandle{spng_ctx_new(0)}) != nullptr);
    CHECK(pvdkit::rpgmvp::libraryVersions().find("libspng 0.7.4, zlib ") == 0);
}

TEST_CASE("adapter contexts install bounded image and chunk limits")
{
    auto bytes = fixture("rgba8_48x48.rpgmvp");
    const auto limits = pvdkit::rpgmvp::detail::configuredLimits(bytes, kOptions);
    REQUIRE(limits.has_value());

    CHECK(limits->imageResult == SPNG_OK);
    CHECK(limits->chunkResult == SPNG_OK);
    CHECK(limits->width == kOptions.maxDimension);
    CHECK(limits->height == kOptions.maxDimension);
    CHECK(limits->chunkBytes == std::size_t{16} * 1024U * 1024U);
    CHECK(limits->cachedChunkBytes == std::size_t{64} * 1024U * 1024U);

    auto invalidOptions = kOptions;
    invalidOptions.maxDimension = std::numeric_limits<std::uint32_t>::max();
    auto rejected = pvdkit::rpgmvp::detail::configuredLimits(bytes, invalidOptions);
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == ErrorCode::Internal);
    CHECK(rejected.error().detail.find("spng_set_image_limits") == 0);
}

TEST_CASE("factory reports metadata for every supported PNG source shape")
{
    constexpr std::array cases{
        MetaCase{"rgba8_36x16_shadow2.rpgmvp", 36, 16, 8, ChromaFormat::Yuv444, true, false, false, false},
        MetaCase{"indexed4_trns_144x192_cursor.rpgmvp", 144, 192, 4, ChromaFormat::Yuv444, true, true, false, false},
        MetaCase{"indexed8_trns_288x384_weapons3.rpgmvp", 288, 384, 8, ChromaFormat::Yuv444, true, true, false, false},
        MetaCase{"rgb8_816x624_gameover.rpgmvp", 816, 624, 8, ChromaFormat::Yuv444, false, false, false, false},
        MetaCase{"rgba16_60x20_par.rpgmvp", 60, 20, 16, ChromaFormat::Yuv444, true, false, false, false},
        MetaCase{"indexed8_trns_82x38_shadow2.png_", 82, 38, 8, ChromaFormat::Yuv444, true, true, false, false},
        MetaCase{"rgba8_576x384_lolded.png_", 576, 384, 8, ChromaFormat::Yuv444, true, false, false, false},
        MetaCase{"rgba8_adam7_700x700_kamen.png_", 700, 700, 8, ChromaFormat::Yuv444, true, false, true, false},
        MetaCase{"rgb16_88x4a.rpgmvp", 88, 4, 16, ChromaFormat::Yuv444, false, false, false, false},
        MetaCase{"rgba8_48x48.rpgmvp", 48, 48, 8, ChromaFormat::Yuv444, true, false, false, false},
        MetaCase{"rgba8_srgb_48x48.rpgmvp", 48, 48, 8, ChromaFormat::Yuv444, true, false, false, true},
        MetaCase{"gray8_16x8.rpgmvp", 16, 8, 8, ChromaFormat::Yuv400, false, false, false, false},
        MetaCase{"graya8_16x8.rpgmvp", 16, 8, 8, ChromaFormat::Yuv400, true, false, false, false},
        MetaCase{"gray1_16x8.rpgmvp", 16, 8, 1, ChromaFormat::Yuv400, false, false, false, false},
        MetaCase{"rgba8_noninterlaced_700x700_kamen.rpgmvp", 700, 700, 8, ChromaFormat::Yuv444, true, false, false,
                 false},
        MetaCase{"apng_4x4.rpgmvp", 4, 4, 8, ChromaFormat::Yuv444, false, false, false, false},
    };

    DecoderFactory factory;
    for (const auto &item : cases) {
        CAPTURE(item.name);
        auto bytes = fixture(item.name);
        CHECK(factory.recognises(bytes));
        auto created = factory.create(bytes, kOptions);
        REQUIRE(created.has_value());
        const auto &meta = (*created)->meta();
        CHECK(meta.width == item.width);
        CHECK(meta.height == item.height);
        CHECK(meta.depth == item.depth);
        CHECK(meta.chroma == item.chroma);
        CHECK(meta.hasAlpha == item.alpha);
        CHECK_FALSE(meta.alphaPremultiplied);
        CHECK(meta.cicp.primaries == (item.srgb ? 1 : 2));
        CHECK(meta.cicp.transfer == (item.srgb ? 13 : 2));
        CHECK(meta.cicp.matrix == 0);
        CHECK(meta.cicp.fullRange);
        CHECK(meta.frameCount == 1);
        CHECK_FALSE(meta.animated);
        CHECK_FALSE(meta.transforms.clap.has_value());
        CHECK(meta.transforms.irotAngle == 0);
        CHECK_FALSE(meta.transforms.imir.has_value());
        const bool expectedIcc = item.name == "rgba16_60x20_par.rpgmvp" || item.name == "rgb16_88x4a.rpgmvp";
        CHECK(meta.hasIcc == expectedIcc);
        CHECK_FALSE(meta.hasExif);
        CHECK_FALSE(meta.hasXmp);
        CHECK(meta.indexed == item.indexed);
        CHECK(meta.interlaced == item.interlaced);
    }
}

TEST_CASE("embedded ICC profiles are detected and do not disturb exact quadrant pixels in the RPGMVP adapter")
{
    DecoderFactory factory;
    for (const auto name : {"icc_swapped_rb_64x64.rpgmvp", "icc_srgb_64x64.rpgmvp"}) {
        CAPTURE(name);
        auto bytes = fixture(name);
        auto created = factory.create(bytes, kOptions);
        REQUIRE(created.has_value());
        CHECK((*created)->meta().hasIcc);

        constexpr std::uint32_t pitch = 64U * 3U;
        std::vector<std::byte> pixels(std::size_t{pitch} * 64U);
        REQUIRE((*created)->decodeFrame(0, PixelFormat::Bgr24, pixels, pitch));
        const auto pixel = [&](const std::uint32_t x, const std::uint32_t y) {
            const auto offset = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * 3U;
            return std::array{pixels[offset], pixels[offset + 1U], pixels[offset + 2U]};
        };
        CHECK(pixel(16, 16) == std::array{std::byte{0}, std::byte{0}, std::byte{255}});
        CHECK(pixel(48, 16) == std::array{std::byte{0}, std::byte{255}, std::byte{0}});
        CHECK(pixel(16, 48) == std::array{std::byte{255}, std::byte{0}, std::byte{0}});
        CHECK(pixel(48, 48) == std::array{std::byte{128}, std::byte{128}, std::byte{128}});
    }
}

TEST_CASE("factory rejects other data and enforces the image dimension limit")
{
    DecoderFactory factory;
    auto plain = fixture("rgba8_48x48.png");
    CHECK_FALSE(factory.recognises(plain));
    auto rejected = factory.create(plain, kOptions);
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == ErrorCode::NotRecognised);

    auto large = fixture("too_large_100000x100000.rpgmvp");
    auto tooLarge = factory.create(large, kOptions);
    REQUIRE_FALSE(tooLarge.has_value());
    CHECK(tooLarge.error().code == ErrorCode::TooLarge);
    CHECK_FALSE(tooLarge.error().detail.empty());

    auto badSrgbBytes = fixture("decode-failures/bad_srgb_crc.rpgmvp");
    auto badSrgb = factory.create(badSrgbBytes, kOptions);
    REQUIRE_FALSE(badSrgb.has_value());
    CHECK(badSrgb.error().code == ErrorCode::ParseFailed);
}

TEST_CASE("frame timing exposes exactly one still frame")
{
    auto bytes = fixture("rgba8_48x48.rpgmvp");
    DecoderFactory factory;
    auto created = factory.create(bytes, kOptions);
    REQUIRE(created.has_value());
    auto first = (*created)->frameTiming(0);
    REQUIRE(first.has_value());
    CHECK(first->durationMs == 0);
    auto second = (*created)->frameTiming(1);
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == ErrorCode::PageOutOfRange);
}

TEST_CASE("destination validation requires the metadata-selected tightly packed format")
{
    pvdkit::core::ImageMeta meta{};
    meta.width = 3;
    meta.height = 2;
    meta.hasAlpha = true;
    CHECK(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgra32, 24, 12));
    CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgr24, 24, 12));
    CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgra32, 24, 11));
    CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgra32, 23, 12));

    meta.hasAlpha = false;
    CHECK(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgr24, 18, 9));
    CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgra32, 24, 12));

    meta.depth = 16;
    CHECK(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgra64, 48, 24));
    CHECK_FALSE(pvdkit::rpgmvp::checkDestination(meta, PixelFormat::Bgra64, 48, 23));
}

TEST_CASE("Bgra64 is rejected cleanly for sources with at most eight bits per sample")
{
    DecoderFactory factory;
    auto bytes = fixture("rgba8_48x48.rpgmvp");
    auto decoder = factory.create(bytes, kOptions);
    REQUIRE(decoder.has_value());
    std::vector<std::byte> pixels(std::size_t{48} * 48U * 8U);

    const auto result = (*decoder)->decodeFrame(0, PixelFormat::Bgra64, pixels, 48U * 8U);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::UnsupportedFeature);
    CHECK(result.error().detail == "BGRA64 output requires a source deeper than 8 bits per sample");
}

TEST_CASE("16-bit RGB and RGBA sources decode to native-endian BGRA64 samples")
{
    DecoderFactory factory;

    auto rgbaBytes = fixture("rgba16_60x20_par.rpgmvp");
    auto rgba = factory.create(rgbaBytes, kOptions);
    REQUIRE(rgba.has_value());
    std::vector<std::byte> rgbaPixels(std::size_t{60} * 20U * 8U);
    REQUIRE((*rgba)->decodeFrame(0, PixelFormat::Bgra64, rgbaPixels, 60U * 8U));
    const std::array rgbaFirst{sample16(rgbaPixels, 0), sample16(rgbaPixels, 1), sample16(rgbaPixels, 2),
                               sample16(rgbaPixels, 3)};
    CHECK(rgbaFirst == std::array<std::uint16_t, 4>{0xFFFF, 0xFFFF, 0xFFFF, 0x0000});

    auto rgbBytes = fixture("rgb16_88x4a.rpgmvp");
    auto rgb = factory.create(rgbBytes, kOptions);
    REQUIRE(rgb.has_value());
    std::vector<std::byte> rgbPixels(std::size_t{88} * 4U * 8U);
    REQUIRE((*rgb)->decodeFrame(0, PixelFormat::Bgra64, rgbPixels, 88U * 8U));
    const std::array rgbFirst{sample16(rgbPixels, 0), sample16(rgbPixels, 1), sample16(rgbPixels, 2),
                              sample16(rgbPixels, 3)};
    CHECK(rgbFirst == std::array<std::uint16_t, 4>{0x3BF4, 0x6060, 0x1B96, 0xFFFF});
}

TEST_CASE("decode expands source formats to BGR or BGRA and reports corrupt IDAT")
{
    DecoderFactory factory;

    auto rgbaBytes = fixture("rgba8_48x48.rpgmvp");
    auto rgba = factory.create(rgbaBytes, kOptions);
    REQUIRE(rgba.has_value());
    std::vector<std::byte> bgra(std::size_t{48} * 48U * 4U);
    CHECK((*rgba)->decodeFrame(0, PixelFormat::Bgra32, bgra, 48U * 4U));
    CHECK(bgra[0] == std::byte{0x00});
    CHECK(bgra[1] == std::byte{0x00});
    CHECK(bgra[2] == std::byte{0x00});
    CHECK(bgra[3] == std::byte{0x00});
    auto missing = (*rgba)->decodeFrame(1, PixelFormat::Bgra32, bgra, 48U * 4U);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::PageOutOfRange);

    auto rgbBytes = fixture("rgb16_88x4a.rpgmvp");
    auto rgb = factory.create(rgbBytes, kOptions);
    REQUIRE(rgb.has_value());
    std::vector<std::byte> bgr(std::size_t{88} * 4U * 3U);
    CHECK((*rgb)->decodeFrame(0, PixelFormat::Bgr24, bgr, 88U * 3U));
    CHECK(bgr[0] == std::byte{0x3B});
    CHECK(bgr[1] == std::byte{0x60});
    CHECK(bgr[2] == std::byte{0x1B});

    auto corruptBytes = fixture("decode-failures/truncated_idat.rpgmvp");
    auto corrupt = factory.create(corruptBytes, kOptions);
    REQUIRE(corrupt.has_value());
    std::vector<std::byte> corruptPixels(std::size_t{48} * 48U * 4U);
    auto failed = (*corrupt)->decodeFrame(0, PixelFormat::Bgra32, corruptPixels, 48U * 4U);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::DecodeFailed);
    CHECK_FALSE(failed.error().detail.empty());
}

TEST_CASE("Adam7 and non-interlaced encodings decode to identical pixels repeatedly")
{
    DecoderFactory factory;
    auto adam7Bytes = fixture("rgba8_adam7_700x700_kamen.png_");
    auto plainBytes = fixture("rgba8_noninterlaced_700x700_kamen.rpgmvp");
    auto adam7 = factory.create(adam7Bytes, kOptions);
    auto plain = factory.create(plainBytes, kOptions);
    REQUIRE(adam7.has_value());
    REQUIRE(plain.has_value());
    std::vector<std::byte> first(std::size_t{700} * 700U * 4U);
    std::vector<std::byte> second(first.size());
    std::vector<std::byte> repeated(first.size());
    CHECK((*adam7)->decodeFrame(0, PixelFormat::Bgra32, first, 700U * 4U));
    CHECK((*plain)->decodeFrame(0, PixelFormat::Bgra32, second, 700U * 4U));
    CHECK((*adam7)->decodeFrame(0, PixelFormat::Bgra32, repeated, 700U * 4U));
    CHECK(first == second);
    CHECK(first == repeated);
}
