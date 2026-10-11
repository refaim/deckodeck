#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "adapters/exr/Context.hpp"
#include "adapters/exr/Decoder.hpp"
#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "core/Windows.hpp"
#include "pvd/Types.hpp"
#include "support/ReferencePipeline.hpp"
#include "support/ReferencePixels.hpp"

namespace
{

    using pvdkit::core::ChromaFormat;
    using pvdkit::core::DecoderOptions;
    using pvdkit::core::ErrorCode;
    using pvdkit::exr::tests::kReferenceFixtures;
    using pvdkit::exr::tests::ReferenceFixture;
    using pvdkit::pvd::PixelFormat;

    constexpr DecoderOptions kOptions{4, false, std::uint64_t{16'384} * 16'384, 32'768, true};

    std::vector<std::byte> readFixture(const std::string_view name)
    {
        const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        return bytes;
    }

    std::string createError(const pvdkit::core::Result<std::unique_ptr<pvdkit::core::IDecoder>> &created)
    {
        return created ? std::string{} : created.error().detail;
    }

    struct Decoded
    {
        std::uint32_t width;
        std::uint32_t height;
        std::vector<std::byte> pixels;

        [[nodiscard]] pvdkit::exr::tests::Rgba16 at(const std::uint32_t x, const std::uint32_t y) const
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 8;
            const auto sample = [&](const std::size_t channel) {
                return static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(pixels[offset + channel * 2]) |
                    (std::to_integer<std::uint8_t>(pixels[offset + channel * 2 + 1]) << 8U));
            };
            return {sample(0), sample(1), sample(2), sample(3)};
        }
    };

    Decoded decode(pvdkit::core::IDecoder &decoder)
    {
        const auto &meta = decoder.meta();
        const std::uint32_t pitch = meta.width * 8;
        std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * meta.height);
        const auto decoded = decoder.decodeFrame(0, PixelFormat::Bgra64, pixels, pitch);
        const std::string decodeError = decoded ? std::string{} : decoded.error().detail;
        CAPTURE(decodeError);
        REQUIRE(decoded.has_value());
        return {meta.width, meta.height, std::move(pixels)};
    }

    std::unique_ptr<pvdkit::core::IDecoder> createFixture(const std::string_view name,
                                                          const DecoderOptions &options = kOptions)
    {
        CAPTURE(name);
        const auto bytes = readFixture(name);
        pvdkit::exr::DecoderFactory factory;
        REQUIRE(factory.recognises(bytes));
        auto decoder = factory.create(bytes, options);
        CAPTURE(createError(decoder));
        REQUIRE(decoder.has_value());
        return std::move(*decoder);
    }

    struct ExpectedMeta
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
        std::uint8_t depth;
        ChromaFormat chroma;
        bool alpha;
        std::uint16_t primaries;
        bool customChromaticities;
        std::string_view compression;
        std::string_view detail; // concise file-wide source summary
    };

    constexpr std::array kExpectedMeta{
        ExpectedMeta{"rgb_half_zip.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_none.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "uncompressed",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_rle.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "RLE",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_zips.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIPS",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_piz.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_pxr24.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "PXR24",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_b44.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "B44",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_b44a.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "B44A",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_dwaa.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "DWAA",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_dwab.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "DWAB",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_htj2k256.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "HTJ2K",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgb_half_htj2k32.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "HTJ2K",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"rgba_premultiplied.exr", 16, 8, 16, ChromaFormat::Yuv444, true, 1, false, "ZIP",
                     "half RGBA, linear Rec.709"},
        ExpectedMeta{"y_float_zips.exr", 16, 8, 32, ChromaFormat::Yuv400, false, 1, false, "ZIPS",
                     "float Y, linear Rec.709"},
        ExpectedMeta{"layer_rgb.exr", 16, 8, 16, ChromaFormat::Yuv444, true, 1, false, "ZIP", "2 pages, layers"},
        ExpectedMeta{"rgb_float_mixed.exr", 16, 8, 32, ChromaFormat::Yuv444, true, 1, false, "ZIP",
                     "float RGBA, linear Rec.709"},
        ExpectedMeta{"data_inside_display.exr", 16, 12, 16, ChromaFormat::Yuv444, true, 1, false, "ZIP",
                     "half RGBA, linear Rec.709"},
        ExpectedMeta{"data_larger_than_display.exr", 8, 6, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"data_offset_partial.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"data_disjoint.exr", 8, 8, 16, ChromaFormat::Yuv444, true, 1, false, "ZIP",
                     "half RGBA, linear Rec.709"},
        ExpectedMeta{"decreasing_y.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"white_luminance_250.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"chroma_ap0.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 2, true, "ZIP",
                     "half RGB, linear ACES AP0"},
        ExpectedMeta{"chroma_ap1.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 2, true, "ZIP",
                     "half RGB, linear ACES AP1"},
        ExpectedMeta{"chroma_rec2020.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 9, false, "ZIP",
                     "half RGB, linear Rec.2020"},
        ExpectedMeta{"chroma_p3d65.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 12, false, "ZIP",
                     "half RGB, linear P3-D65"},
        ExpectedMeta{"chroma_custom.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 2, true, "ZIP",
                     "half RGB, linear custom"},
        ExpectedMeta{"interop_lin_ap1.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 2, true, "ZIP",
                     "half RGB, linear ACES AP1"},
        ExpectedMeta{"interop_unknown.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"peak_above_10000.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"nan_inf_negative.exr", 16, 8, 32, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "float RGB, linear Rec.709"},
        ExpectedMeta{"multipart_views.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "2 pages, multipart, multiview"},
        ExpectedMeta{"multipart_deep_flat.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"multipart_deeptile_flat.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"multiview_string.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"multiview_left_first.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "2 pages, multiview"},
        ExpectedMeta{"multiview_right_first.exr", 8, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "2 pages, multiview"},
        ExpectedMeta{"tiled_rgba_3x3.exr", 8, 8, 16, ChromaFormat::Yuv444, true, 1, false, "ZIP",
                     "half RGBA, linear Rec.709"},
        ExpectedMeta{"tiled_data_offset.exr", 16, 16, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"chroma_zips.exr", 16, 8, 16, ChromaFormat::Yuv420, true, 1, false, "ZIPS",
                     "half Y+chroma+A, linear Rec.709"},
        ExpectedMeta{"chroma_extra_channel.exr", 16, 8, 16, ChromaFormat::Yuv420, false, 1, false, "ZIP",
                     "half Y+chroma, linear Rec.709"},
        ExpectedMeta{"chroma_missing_by.exr", 16, 8, 16, ChromaFormat::Yuv400, false, 1, false, "ZIP",
                     "half Y, linear Rec.709"},
        ExpectedMeta{"t01.exr", 400, 300, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"t02.exr", 400, 300, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"t05.exr", 340, 260, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"t07.exr", 481, 371, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"t09.exr", 200, 300, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"t13.exr", 101, 101, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"t14.exr", 101, 101, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ", "half RGB, linear Rec.709"},
        ExpectedMeta{"AllHalfValues.exr", 256, 256, 16, ChromaFormat::Yuv444, false, 1, false, "PIZ",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"BrightRingsNanInf.exr", 800, 800, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"GammaChart.exr", 800, 800, 16, ChromaFormat::Yuv444, false, 1, false, "PXR24",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"GrayRampsHorizontal.exr", 800, 800, 16, ChromaFormat::Yuv400, false, 1, false, "PXR24",
                     "half Y, linear Rec.709"},
        ExpectedMeta{"WideColorGamut.exr", 800, 800, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"WideFloatRange.exr", 500, 500, 32, ChromaFormat::Yuv400, false, 1, false, "PXR24",
                     "float grey, linear Rec.709"},
        ExpectedMeta{"stripes.exr", 100, 50, 16, ChromaFormat::Yuv444, true, 1, false, "PIZ",
                     "half RGBA, linear Rec.709"},
        ExpectedMeta{"Garden.exr", 874, 493, 16, ChromaFormat::Yuv400, false, 1, false, "PIZ",
                     "half Y, linear Rec.709"},
        ExpectedMeta{"ColorCodedLevels.exr", 512, 512, 16, ChromaFormat::Yuv444, true, 1, false, "PXR24",
                     "half RGBA, linear Rec.709"},
        ExpectedMeta{"PeriodicPattern.exr", 517, 517, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709"},
        ExpectedMeta{"Rec709_YC.exr", 610, 406, 16, ChromaFormat::Yuv420, false, 1, false, "PIZ",
                     "half Y+chroma, linear Rec.709"},
        ExpectedMeta{"XYZ_YC.exr", 610, 406, 16, ChromaFormat::Yuv420, false, 2, true, "PIZ",
                     "half Y+chroma, linear CIE XYZ"},
        ExpectedMeta{"chroma_xyz.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 2, true, "ZIP",
                     "half RGB, linear CIE XYZ"},
        ExpectedMeta{"rgb_whitey0.exr", 16, 8, 16, ChromaFormat::Yuv444, false, 1, false, "ZIP",
                     "half RGB, linear Rec.709 assumed"},
        ExpectedMeta{"yc_whitey0.exr", 16, 8, 16, ChromaFormat::Yuv420, false, 1, false, "ZIP",
                     "half Y+chroma, linear Rec.709 assumed"},
        ExpectedMeta{"yc_collinear.exr", 16, 8, 16, ChromaFormat::Yuv420, false, 1, false, "ZIP",
                     "half Y+chroma, linear Rec.709 assumed"},
    };

} // namespace

TEST_CASE("every accepted fixture exposes the header facts, the colour signal and the info line")
{
    for (const auto &expected : kExpectedMeta) {
        CAPTURE(expected.name);
        const auto decoder = createFixture(expected.name);
        const auto &meta = decoder->meta();
        CAPTURE(meta.sourceDetail);
        CHECK(meta.width == expected.width);
        CHECK(meta.height == expected.height);
        CHECK(meta.depth == expected.depth);
        CHECK(meta.chroma == expected.chroma);
        CHECK(meta.hasAlpha == expected.alpha);
        CHECK(meta.alphaPremultiplied == expected.alpha);
        CHECK(meta.cicp.primaries == expected.primaries);
        CHECK(meta.cicp.transfer == 16);
        CHECK(meta.cicp.matrix == 0);
        CHECK(meta.cicp.fullRange);
        CHECK(meta.chromaticities.has_value() == expected.customChromaticities);
        const std::array<std::string_view, 4> twoPages{"layer_rgb.exr", "multipart_views.exr",
                                                       "multiview_left_first.exr", "multiview_right_first.exr"};
        CHECK(meta.frameCount == (std::ranges::find(twoPages, expected.name) != twoPages.end() ? 2U : 1U));
        CHECK_FALSE(meta.animated);
        CHECK(meta.compression == expected.compression);
        CHECK(meta.sourceDetail == expected.detail);
        REQUIRE(meta.masteringPeakNits.has_value());
        CHECK(*meta.masteringPeakNits >= 100.0F);
        CHECK(*meta.masteringPeakNits <= 10'000.0F);
        CHECK_FALSE(meta.hasIcc);
        CHECK(meta.exifOrientation == 0);
    }
}

TEST_CASE("the tone-mapping peak matches the generator's independent percentile")
{
    for (const auto &fixture : kReferenceFixtures) {
        CAPTURE(fixture.name);
        const auto decoder = createFixture(fixture.name);
        const auto peak = decoder->meta().masteringPeakNits;
        REQUIRE(peak.has_value());
        // The plugin reads the percentile from a 16-bit PQ histogram: the bin's lower edge is
        // within one code of the exact value, about 0.1 % in nits at the dark end.
        CHECK(*peak == doctest::Approx(fixture.peakNits).epsilon(2.0e-3));
    }
}

TEST_CASE("decoded PQ codes match the double-precision reference at every recorded pixel")
{
    for (const auto &fixture : kReferenceFixtures) {
        CAPTURE(fixture.name);
        const auto decoder = createFixture(fixture.name);
        REQUIRE(decoder->meta().width == fixture.width);
        REQUIRE(decoder->meta().height == fixture.height);
        const auto decoded = decode(*decoder);
        for (const auto &sample : fixture.samples) {
            CAPTURE(sample.x);
            CAPTURE(sample.y);
            const auto expected = pvdkit::exr::tests::expectedPqCodes(sample, fixture);
            const auto actual = decoded.at(sample.x, sample.y);
            CHECK(std::abs(static_cast<int>(actual.b) - static_cast<int>(expected.b)) <= 1);
            CHECK(std::abs(static_cast<int>(actual.g) - static_cast<int>(expected.g)) <= 1);
            CHECK(std::abs(static_cast<int>(actual.r) - static_cast<int>(expected.r)) <= 1);
            CHECK(actual.a == expected.a);
        }
    }
}

TEST_CASE("the display window is composited with black or transparent black around the data")
{
    const auto disjoint = createFixture("data_disjoint.exr");
    const auto transparent = decode(*disjoint);
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            const auto pixel = transparent.at(x, y);
            CHECK(pixel.r == 0);
            CHECK(pixel.g == 0);
            CHECK(pixel.b == 0);
            CHECK(pixel.a == 0);
        }
    }

    const auto opaque = createFixture("t09.exr");
    const auto black = decode(*opaque);
    CHECK(black.at(0, 0).a == 65'535);
    CHECK(black.at(0, 0).r == 0);
    CHECK(black.at(199, 299).a == 65'535);
    CHECK(black.at(199, 299).g == 0);

    // The one common pixel of t13 is the data window's lower right at the display's upper left.
    const auto corner = createFixture("t13.exr");
    const auto one = decode(*corner);
    CHECK(one.at(0, 0).a == 65'535);
    CHECK(one.at(1, 0).r == 0);
    CHECK(one.at(0, 1).r == 0);
}

TEST_CASE("rejected fixtures fail with the category that names why")
{
    struct Rejection
    {
        std::string_view name;
        ErrorCode code;
    };
    constexpr std::array rejections{
        Rejection{"uint_only.exr", ErrorCode::UnsupportedFeature},
        Rejection{"deep_only.exr", ErrorCode::UnsupportedFeature},
        Rejection{"huge_display.exr", ErrorCode::TooLarge},
        Rejection{"display_over_maxpixels.exr", ErrorCode::TooLarge},
        Rejection{"tile_too_large.exr", ErrorCode::TooLarge},
        Rejection{"yc_huge_data.exr", ErrorCode::TooLarge},
        Rejection{"truncated.exr", ErrorCode::DecodeFailed},
        Rejection{"corrupt_chunk.exr", ErrorCode::DecodeFailed},
        Rejection{"chroma_corrupt_chunk.exr", ErrorCode::DecodeFailed},
        Rejection{"magic_only.exr", ErrorCode::ParseFailed},
        Rejection{"garbage.bin", ErrorCode::NotRecognised},
        Rejection{"not_exr.txt", ErrorCode::NotRecognised},
    };
    pvdkit::exr::DecoderFactory factory;
    for (const auto &rejection : rejections) {
        CAPTURE(rejection.name);
        const auto bytes = readFixture(rejection.name);
        const auto created = factory.create(bytes, kOptions);
        REQUIRE_FALSE(created.has_value());
        CAPTURE(created.error().detail);
        CHECK(created.error().code == rejection.code);
        CHECK_FALSE(created.error().detail.empty());
    }
    // Recognition needs the magic number, version 2 and a head of at least 16 bytes.
    CHECK_FALSE(factory.recognises({}));
    const auto valid = readFixture("rgb_half_zip.exr");
    CHECK_FALSE(factory.recognises(std::span{valid}.first(15)));
    CHECK(factory.recognises(std::span{valid}.first(16)));
    auto wrongVersion = valid;
    wrongVersion[4] = std::byte{3};
    CHECK_FALSE(factory.recognises(wrongVersion));
    auto wrongMagic = valid;
    wrongMagic[0] = std::byte{0x77};
    CHECK_FALSE(factory.recognises(wrongMagic));
}

TEST_CASE("the decoder limits refuse pictures beyond them before any pixel is read")
{
    const auto tooWide = [](const std::string_view name, const DecoderOptions &options) {
        pvdkit::exr::DecoderFactory factory;
        const auto bytes = readFixture(name);
        const auto created = factory.create(bytes, options);
        REQUIRE_FALSE(created.has_value());
        CAPTURE(created.error().detail);
        CHECK(created.error().code == ErrorCode::TooLarge);
    };
    // A 16x8 display window against a dimension limit of 8 (the data window trips the Core's own
    // check first) and against a pixel budget of 100.
    tooWide("rgb_half_zip.exr", DecoderOptions{1, false, 1'000, 8, true});
    tooWide("rgb_half_zip.exr", DecoderOptions{1, false, 100, 32, true});
    // A display window larger than the data window: only the display window exceeds the budget.
    tooWide("data_inside_display.exr", DecoderOptions{1, false, 100, 32, true});
    // Tiles wider than the per-context tile limit are refused at header time.
    tooWide("Garden.exr", DecoderOptions{1, false, std::uint64_t{16'384} * 16'384, 100, true});
}

TEST_CASE("frame timing and decoding refuse anything but frame 0 and a BGRA64 destination")
{
    const auto decoder = createFixture("rgb_half_zip.exr");
    const auto timing = decoder->frameTiming(0);
    REQUIRE(timing.has_value());
    CHECK(timing->durationMs == 0);
    const auto outOfRange = decoder->frameTiming(1);
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().code == ErrorCode::PageOutOfRange);

    std::vector<std::byte> pixels(16 * 8 * 8);
    const auto frame = decoder->decodeFrame(1, PixelFormat::Bgra64, pixels, 16 * 8);
    REQUIRE_FALSE(frame.has_value());
    CHECK(frame.error().code == ErrorCode::PageOutOfRange);
    const auto format = decoder->decodeFrame(0, PixelFormat::Bgr24, pixels, 16 * 3);
    REQUIRE_FALSE(format.has_value());
    CHECK(format.error().code == ErrorCode::Internal);
    const auto pitch = decoder->decodeFrame(0, PixelFormat::Bgra64, pixels, 16 * 8 - 1);
    REQUIRE_FALSE(pitch.has_value());
    CHECK(pitch.error().code == ErrorCode::Internal);
    const auto small = decoder->decodeFrame(0, PixelFormat::Bgra64, std::span{pixels}.first(pixels.size() - 1), 16 * 8);
    REQUIRE_FALSE(small.has_value());
    CHECK(small.error().code == ErrorCode::Internal);

    // A padded pitch is honoured and the picture can be decoded again.
    std::vector<std::byte> padded(16 * 8 * 8 + 8 * 8);
    REQUIRE(decoder->decodeFrame(0, PixelFormat::Bgra64, padded, 16 * 8 + 8).has_value());
    const auto tight = decode(*decoder);
    CHECK(std::equal(tight.pixels.begin(), tight.pixels.begin() + 16 * 8, padded.begin()));
    CHECK(std::equal(tight.pixels.begin() + 16 * 8, tight.pixels.begin() + 2 * 16 * 8, padded.begin() + 16 * 8 + 8));
}

TEST_CASE("the checkDestination seam covers every violation")
{
    using pvdkit::exr::detail::checkDestination;
    const pvdkit::exr::Layout layout{4, 2, 0, 0, std::nullopt};
    CHECK(checkDestination(layout, PixelFormat::Bgra64, 64, 32).has_value());
    CHECK(checkDestination(layout, PixelFormat::Bgra64, 80, 40).has_value());
    CHECK(checkDestination(layout, PixelFormat::Bgra32, 64, 32).error().code == ErrorCode::Internal);
    CHECK(checkDestination(layout, PixelFormat::Bgra64, 64, 31).error().code == ErrorCode::Internal);
    CHECK(checkDestination(layout, PixelFormat::Bgra64, 63, 32).error().code == ErrorCode::Internal);
}

TEST_CASE("decreasing line order and every compression decode to the same picture as ZIP")
{
    const auto reference = decode(*createFixture("rgb_half_zip.exr"));
    for (const auto name :
         {"rgb_half_none.exr", "rgb_half_rle.exr", "rgb_half_zips.exr", "rgb_half_piz.exr", "decreasing_y.exr"}) {
        CAPTURE(name);
        CHECK(decode(*createFixture(name)).pixels == reference.pixels);
    }
}

TEST_CASE("special values are sanitized: NaN and negatives are black, infinities saturate")
{
    const auto decoded = decode(*createFixture("nan_inf_negative.exr"));
    CHECK(decoded.at(0, 0).r == 0);      // NaN red
    CHECK(decoded.at(1, 0).g == 65'535); // +inf green
    CHECK(decoded.at(2, 0).b == 0);      // -inf blue
    CHECK(decoded.at(3, 0).r == 0);      // negative red
    CHECK(decoded.at(4, 0).g == 0);      // NaN green
    CHECK(decoded.at(5, 0).b == 65'535); // +inf blue
    CHECK(decoded.at(0, 0).a == 65'535);
    const auto peak = createFixture("peak_above_10000.exr")->meta().masteringPeakNits;
    REQUIRE(peak.has_value());
    CHECK(*peak == 10'000.0F);
}

TEST_CASE("the library versions the comments quote are the linked ones")
{
    const auto versions = pvdkit::exr::libraryVersions();
    CAPTURE(versions);
    CHECK(versions.find("OpenEXR 3.4.13") != std::string::npos);
    CHECK(versions.find("Imath 3.2.2") != std::string::npos);
    CHECK(versions.find("libdeflate 1.25") != std::string::npos);
    CHECK(versions.find("OpenJPH 0.30.1") != std::string::npos);
}
