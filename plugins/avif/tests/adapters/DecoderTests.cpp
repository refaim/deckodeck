#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <avif/avif.h>
#include <doctest/doctest.h>

#include "adapters/avif/Decoder.hpp"
#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"

namespace
{

    using pvdkit::core::ChromaFormat;
    using pvdkit::core::DecoderOptions;
    using pvdkit::core::ErrorCode;
    using pvdkit::pvd::PixelFormat;

    constexpr std::uint64_t kLibavifSizeLimit = 16384ULL * 16384ULL;
    constexpr DecoderOptions kOptions{4, false, kLibavifSizeLimit, 32768};

    using Bgr = std::array<std::uint8_t, 3>;
    using Bgra = std::array<std::uint8_t, 4>;
    using Bgra16 = std::array<std::uint16_t, 4>;

    std::vector<std::byte> readFixture(const std::string_view name)
    {
        const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
        const auto size = std::filesystem::file_size(path);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
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

    // Offset of the only occurrence of the four-character box type `type` inside `bytes`.
    std::size_t boxTypeOffset(const std::span<const std::byte> bytes, const std::string_view type)
    {
        const auto asBytes = std::as_bytes(std::span{type});
        const auto first = std::ranges::search(bytes, asBytes);
        REQUIRE_FALSE(first.empty());
        const auto rest = bytes.subspan(static_cast<std::size_t>(first.end() - bytes.begin()));
        REQUIRE(std::ranges::search(rest, asBytes).empty());
        return static_cast<std::size_t>(first.begin() - bytes.begin());
    }

    struct ExpectedMeta
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
        std::uint8_t depth;
        ChromaFormat chroma;
        bool alpha;
        std::uint32_t frames;
        bool clap;
        std::uint8_t irotAngle;
        bool imir;
        bool icc;
        bool exif;
        bool xmp;
        std::uint8_t exifOrientation = 0;
    };

    constexpr std::array kExpectedMeta{
        ExpectedMeta{"white_1x1.avif", 1, 1, 8, ChromaFormat::Yuv444, false, 1, false, 0, false, false, false, false},
        ExpectedMeta{"kodim03_yuv420_8bpc.avif", 768, 512, 8, ChromaFormat::Yuv420, false, 1, false, 0, false, false,
                     false, false},
        ExpectedMeta{"kodim03_exif_orientation_6.avif", 768, 512, 8, ChromaFormat::Yuv420, false, 1, false, 0, false,
                     false, true, false, 6},
        ExpectedMeta{"kodim03_exif_orientation_3.avif", 768, 512, 8, ChromaFormat::Yuv420, false, 1, false, 0, false,
                     false, true, false, 3},
        ExpectedMeta{"cosmos1650_yuv444_10bpc_p3pq.avif", 1024, 428, 10, ChromaFormat::Yuv444, false, 1, false, 0,
                     false, false, false, false},
        ExpectedMeta{"alpha_noispe.avif", 80, 80, 8, ChromaFormat::Yuv444, true, 1, false, 0, false, false, false,
                     false},
        ExpectedMeta{"abc_color_irot_alpha_irot.avif", 512, 256, 8, ChromaFormat::Yuv444, true, 1, false, 1, false,
                     false, false, false},
        ExpectedMeta{"abc_color_irot_alpha_irot_plus_exif6.avif", 512, 256, 8, ChromaFormat::Yuv444, true, 1, false, 1,
                     false, false, true, false, 0},
        ExpectedMeta{"abc_color_irot_alpha_NOirot.avif", 512, 256, 8, ChromaFormat::Yuv444, true, 1, false, 1, false,
                     false, false, false},
        ExpectedMeta{"clop_irot_imor.avif", 12, 34, 10, ChromaFormat::Yuv444, true, 1, false, 1, false, false, false,
                     false},
        ExpectedMeta{"sofa_grid1x5_420.avif", 1024, 770, 8, ChromaFormat::Yuv420, false, 1, false, 0, false, false,
                     false, false},
        ExpectedMeta{"color_grid_alpha_nogrid.avif", 80, 80, 8, ChromaFormat::Yuv444, true, 1, false, 0, false, false,
                     false, false},
        ExpectedMeta{"colors-animated-8bpc.avif", 150, 150, 8, ChromaFormat::Yuv420, false, 5, false, 0, false, false,
                     false, false},
        ExpectedMeta{"colors-animated-8bpc-alpha-exif-xmp.avif", 150, 150, 8, ChromaFormat::Yuv420, true, 5, false, 0,
                     false, false, true, true, 1},
        ExpectedMeta{"colors-animated-12bpc-keyframes-0-2-3.avif", 64, 64, 12, ChromaFormat::Yuv422, true, 5, false, 0,
                     false, false, false, false},
        ExpectedMeta{"colors_hdr_rec2020.avif", 200, 200, 10, ChromaFormat::Yuv444, false, 1, false, 0, false, false,
                     true, true},
        ExpectedMeta{"colors_sdr_srgb.avif", 200, 200, 8, ChromaFormat::Yuv444, false, 1, false, 0, false, false, false,
                     false},
        ExpectedMeta{"paris_icc_exif_xmp.avif", 403, 302, 8, ChromaFormat::Yuv444, false, 1, false, 0, false, true,
                     true, true, 1},
        ExpectedMeta{"draw_points_idat_progressive.avif", 33, 11, 8, ChromaFormat::Yuv444, true, 1, false, 0, false,
                     false, false, false},
        ExpectedMeta{"extended_pixi.avif", 4, 4, 8, ChromaFormat::Yuv420, false, 1, false, 0, false, false, false,
                     false},
        ExpectedMeta{"weld_sato_12B_8B_q0.avif", 1024, 684, 12, ChromaFormat::Yuv444, false, 1, false, 0, false, false,
                     false, false},
        ExpectedMeta{"quad_rgb_lossless.avif", 64, 64, 8, ChromaFormat::Yuv444, false, 1, false, 0, false, false, false,
                     false},
        ExpectedMeta{"quad_yuv420.avif", 64, 64, 8, ChromaFormat::Yuv420, false, 1, false, 0, false, false, false,
                     false},
        ExpectedMeta{"alpha_steps.avif", 96, 32, 8, ChromaFormat::Yuv444, true, 1, false, 0, false, false, false,
                     false},
        ExpectedMeta{"anim_3frames.avif", 64, 64, 8, ChromaFormat::Yuv444, false, 3, false, 0, false, false, false,
                     false},
        ExpectedMeta{"gray_400.avif", 64, 64, 8, ChromaFormat::Yuv400, false, 1, false, 0, false, false, false, false},
        ExpectedMeta{"tenbit_444.avif", 64, 64, 10, ChromaFormat::Yuv444, false, 1, false, 0, false, false, false,
                     false},
    };

    template <std::size_t Channels>
    std::array<std::uint8_t, Channels> pixelAt(const std::span<const std::byte> pixels, const std::uint32_t pitch,
                                               const std::uint32_t x, const std::uint32_t y)
    {
        const std::size_t offset = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * Channels;
        std::array<std::uint8_t, Channels> result{};
        for (std::size_t channel = 0; channel < Channels; ++channel) {
            result[channel] = std::to_integer<std::uint8_t>(pixels[offset + channel]);
        }
        return result;
    }

    template <std::size_t Channels>
    std::array<std::uint16_t, Channels> pixelAt16(const std::span<const std::byte> pixels, const std::uint32_t pitch,
                                                  const std::uint32_t x, const std::uint32_t y)
    {
        const std::size_t offset = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * Channels * 2;
        std::array<std::uint16_t, Channels> result{};
        for (std::size_t channel = 0; channel < Channels; ++channel) {
            const auto sample = pixels.subspan(offset + channel * 2, 2);
            result[channel] = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(sample[0])) |
                              static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(sample[1]) << 8U);
        }
        return result;
    }

    template <std::size_t Channels> std::string describe(const std::array<std::uint8_t, Channels> &pixel)
    {
        std::string text{"("};
        for (std::size_t channel = 0; channel < Channels; ++channel) {
            text += (channel == 0 ? "" : ",") + std::to_string(pixel[channel]);
        }
        return text + ")";
    }

    template <std::size_t Channels>
    bool within(const std::array<std::uint8_t, Channels> &actual, const std::array<std::uint8_t, Channels> &expected,
                const int tolerance)
    {
        const std::string actualText = describe(actual);
        const std::string expectedText = describe(expected);
        CAPTURE(actualText);
        CAPTURE(expectedText);
        for (std::size_t channel = 0; channel < Channels; ++channel) {
            const int difference = std::abs(static_cast<int>(actual[channel]) - static_cast<int>(expected[channel]));
            CHECK(difference <= tolerance);
            if (difference > tolerance) {
                return false;
            }
        }
        return true;
    }

    struct DecodedBgr
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t pitch;
        std::vector<std::byte> pixels;

        [[nodiscard]] Bgr at(const std::uint32_t x, const std::uint32_t y) const
        {
            return pixelAt<3>(pixels, pitch, x, y);
        }
        [[nodiscard]] bool uniform() const
        {
            return uniform(pixels);
        }
        [[nodiscard]] bool rowUniform(const std::uint32_t y) const
        {
            return uniform(std::span{pixels}.subspan(static_cast<std::size_t>(y) * pitch, pitch));
        }

      private:
        [[nodiscard]] static bool uniform(const std::span<const std::byte> bytes)
        {
            return std::ranges::adjacent_find(bytes, std::ranges::not_equal_to{}) == bytes.end();
        }
    };

    DecodedBgr decodeBgr(pvdkit::core::IDecoder &decoder, const std::uint32_t frame)
    {
        const auto &meta = decoder.meta();
        const std::uint32_t pitch = meta.width * 3;
        std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * meta.height);
        const auto decoded = decoder.decodeFrame(frame, PixelFormat::Bgr24, pixels, pitch);
        const std::string decodeError = decoded ? std::string{} : decoded.error().detail;
        CAPTURE(decodeError);
        REQUIRE(decoded.has_value());
        return {meta.width, meta.height, pitch, std::move(pixels)};
    }

    DecodedBgr decodeFixtureBgr(const std::string_view name, const unsigned threads = kOptions.maxThreads)
    {
        CAPTURE(name);
        const auto bytes = readFixture(name);
        pvdkit::avif::DecoderFactory factory;
        const DecoderOptions options{threads, false, kOptions.maxPixels, kOptions.maxDimension};
        auto decoder = factory.create(bytes, options);
        CAPTURE(createError(decoder));
        REQUIRE(decoder.has_value());
        return decodeBgr(**decoder, 0);
    }

    struct DecodedBgra64
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t pitch;
        std::vector<std::byte> pixels;

        [[nodiscard]] Bgra16 at(const std::uint32_t x, const std::uint32_t y) const
        {
            return pixelAt16<4>(pixels, pitch, x, y);
        }
    };

    DecodedBgra64 decodeFixtureBgra64(const std::string_view name, const std::uint32_t frame = 0)
    {
        CAPTURE(name);
        const auto bytes = readFixture(name);
        pvdkit::avif::DecoderFactory factory;
        auto decoder = factory.create(bytes, kOptions);
        CAPTURE(createError(decoder));
        REQUIRE(decoder.has_value());
        const auto &meta = (*decoder)->meta();
        const std::uint32_t pitch = meta.width * 8;
        std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * meta.height);
        const auto decoded = (*decoder)->decodeFrame(frame, PixelFormat::Bgra64, pixels, pitch);
        const std::string decodeError = decoded ? std::string{} : decoded.error().detail;
        CAPTURE(decodeError);
        REQUIRE(decoded.has_value());
        return {meta.width, meta.height, pitch, std::move(pixels)};
    }

    struct AvifDecoderDestroy
    {
        void operator()(avifDecoder *decoder) const noexcept
        {
            avifDecoderDestroy(decoder);
        }
    };

    struct SourceAlpha
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t depth;
        std::vector<std::uint16_t> samples;

        [[nodiscard]] std::uint16_t at(const std::uint32_t x, const std::uint32_t y) const
        {
            return samples[static_cast<std::size_t>(y) * width + x];
        }
    };

    SourceAlpha decodeSourceAlpha(const std::span<const std::byte> bytes, const std::uint32_t frame)
    {
        const auto decoder = std::unique_ptr<avifDecoder, AvifDecoderDestroy>{avifDecoderCreate()};
        REQUIRE(decoder != nullptr);
        REQUIRE(avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const std::uint8_t *>(bytes.data()),
                                       bytes.size()) == AVIF_RESULT_OK);
        REQUIRE(avifDecoderParse(decoder.get()) == AVIF_RESULT_OK);
        REQUIRE(avifDecoderNthImage(decoder.get(), frame) == AVIF_RESULT_OK);
        const avifImage &image = *decoder->image;
        REQUIRE(image.alphaPlane != nullptr);
        REQUIRE(image.alphaRowBytes >= image.width * 2);
        std::vector<std::uint16_t> samples(static_cast<std::size_t>(image.width) * image.height);
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                std::uint16_t sample = 0;
                std::memcpy(&sample,
                            image.alphaPlane + static_cast<std::size_t>(y) * image.alphaRowBytes +
                                static_cast<std::size_t>(x) * 2,
                            sizeof(sample));
                samples[static_cast<std::size_t>(y) * image.width + x] = sample;
            }
        }
        return {image.width, image.height, image.depth, std::move(samples)};
    }

    std::uint16_t rescaleTo16(const std::uint16_t sample, const std::uint32_t sourceDepth)
    {
        const auto sourceMaximum = static_cast<float>((1U << sourceDepth) - 1U);
        return static_cast<std::uint16_t>(std::lround(static_cast<float>(sample) / sourceMaximum * 65535.0F));
    }

} // namespace

TEST_CASE("all positive fixtures expose complete container metadata")
{
    pvdkit::avif::DecoderFactory factory;
    for (const auto &expected : kExpectedMeta) {
        CAPTURE(expected.name);
        const auto bytes = readFixture(expected.name);
        auto created = factory.create(bytes, kOptions);
        CAPTURE(createError(created));
        REQUIRE(created.has_value());
        const auto &meta = (*created)->meta();
        CHECK(meta.width == expected.width);
        CHECK(meta.height == expected.height);
        CHECK(meta.depth == expected.depth);
        CHECK(meta.chroma == expected.chroma);
        CHECK(meta.hasAlpha == expected.alpha);
        CHECK_FALSE(meta.alphaPremultiplied);
        CHECK(meta.frameCount == expected.frames);
        CHECK(meta.animated == (expected.frames > 1));
        CHECK(meta.transforms.clap.has_value() == expected.clap);
        CHECK(meta.transforms.irotAngle == expected.irotAngle);
        CHECK(meta.transforms.imir.has_value() == expected.imir);
        CHECK(meta.hasIcc == expected.icc);
        CHECK(meta.hasExif == expected.exif);
        CHECK(meta.hasXmp == expected.xmp);
        CHECK(meta.exifOrientation == expected.exifOrientation);
        const auto expectedPeak = expected.name == "colors_hdr_rec2020.avif" ? std::optional{470.0F} : std::nullopt;
        CHECK(meta.masteringPeakNits == expectedPeak);
    }
}

TEST_CASE("EXIF orientation parsing accepts only values 1..8 and yields to irot or imir")
{
    for (std::uint8_t orientation = 1; orientation <= 8; ++orientation) {
        CHECK(pvdkit::avif::detail::normalizedExifOrientation(orientation) == orientation);
    }
    CHECK(pvdkit::avif::detail::normalizedExifOrientation(0) == 0);
    CHECK(pvdkit::avif::detail::normalizedExifOrientation(9) == 0);
    CHECK(pvdkit::avif::detail::normalizedExifOrientation(255) == 0);

    auto bytes = readFixture("kodim03_exif_orientation_6.avif");
    const auto decoder = std::unique_ptr<avifDecoder, AvifDecoderDestroy>{avifDecoderCreate()};
    REQUIRE(decoder != nullptr);
    REQUIRE(avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()) ==
            AVIF_RESULT_OK);
    REQUIRE(avifDecoderParse(decoder.get()) == AVIF_RESULT_OK);
    REQUIRE(pvdkit::avif::detail::exifOrientation(*decoder->image) == 6);

    std::size_t offset = decoder->image->exif.size;
    REQUIRE(avifGetExifOrientationOffset(decoder->image->exif.data, decoder->image->exif.size, &offset) ==
            AVIF_RESULT_OK);
    REQUIRE(offset < decoder->image->exif.size);
    for (const auto transform : {AVIF_TRANSFORM_IROT, AVIF_TRANSFORM_IMIR}) {
        decoder->image->transformFlags = transform;
        CHECK(pvdkit::avif::detail::exifOrientation(*decoder->image) == 0);
    }

    avifImage invalidExif{};
    std::array<std::uint8_t, 1> invalidPayload{};
    invalidExif.exif = avifRWData{invalidPayload.data(), invalidPayload.size()};
    CHECK(pvdkit::avif::detail::exifOrientation(invalidExif) == 0);
}

TEST_CASE("AVIF CLLI maxCLL supplies the HDR mastering peak")
{
    avifImage image{};
    CHECK_FALSE(pvdkit::avif::detail::masteringPeakNits(image).has_value());

    image.clli.maxPALL = 400;
    CHECK_FALSE(pvdkit::avif::detail::masteringPeakNits(image).has_value());

    image.clli.maxCLL = 1'000;
    REQUIRE(pvdkit::avif::detail::masteringPeakNits(image).has_value());
    CHECK(*pvdkit::avif::detail::masteringPeakNits(image) == 1'000.0F);
}

TEST_CASE("CICP signalling is preserved for P3/PQ, Rec.2020 and identity sources")
{
    pvdkit::avif::DecoderFactory factory;

    auto cosmosBytes = readFixture("cosmos1650_yuv444_10bpc_p3pq.avif");
    auto cosmos = factory.create(cosmosBytes, kOptions);
    REQUIRE(cosmos.has_value());
    CHECK((*cosmos)->meta().cicp.primaries == 12);
    CHECK((*cosmos)->meta().cicp.transfer == 16);

    auto hdrBytes = readFixture("colors_hdr_rec2020.avif");
    auto hdr = factory.create(hdrBytes, kOptions);
    REQUIRE(hdr.has_value());
    CHECK((*hdr)->meta().cicp.primaries == 9);
    CHECK((*hdr)->meta().cicp.transfer == 16);

    auto rgbBytes = readFixture("quad_rgb_lossless.avif");
    auto rgb = factory.create(rgbBytes, kOptions);
    REQUIRE(rgb.has_value());
    CHECK((*rgb)->meta().cicp.matrix == 0);
    CHECK((*rgb)->meta().cicp.fullRange);
}

TEST_CASE("a real imir property is reported as a mirror axis by the parser")
{
    // No committed fixture carries a parseable `imir`, so derive one: the 9-byte `irot` box of
    // abc_color_irot_alpha_irot.avif (angle byte 0x01) has the same layout as an `imir` box
    // (axis byte 0x01), and both items share that single property, so renaming the box type turns
    // the file into a valid left/right-mirrored image without touching any size or association.
    auto bytes = readFixture("abc_color_irot_alpha_irot.avif");
    const auto offset = boxTypeOffset(bytes, "irot");
    REQUIRE(std::to_integer<std::uint8_t>(bytes[offset + 4]) == 0x01);
    std::ranges::copy(std::as_bytes(std::span{std::string_view{"imir"}}),
                      bytes.begin() + static_cast<std::ptrdiff_t>(offset));

    pvdkit::avif::DecoderFactory factory;
    auto mirrored = factory.create(bytes, kOptions);
    CAPTURE(createError(mirrored));
    REQUIRE(mirrored.has_value());
    CHECK((*mirrored)->meta().transforms.irotAngle == 0);
    REQUIRE((*mirrored)->meta().transforms.imir.has_value());
    CHECK(*(*mirrored)->meta().transforms.imir == pvdkit::core::MirrorAxis::LeftRight);
}

TEST_CASE("libavif rejects known transforms marked non-essential")
{
    const auto bytes = readFixture("clap_irot_imir_non_essential.avif");
    pvdkit::avif::DecoderFactory factory;
    const auto created = factory.create(bytes, kOptions);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code == ErrorCode::ParseFailed);
    CHECK(created.error().detail.find("clap") != std::string::npos);
}

TEST_CASE("an alpha item whose transforms differ from the colour item is UnsupportedFeature")
{
    // libavif 1.4.2 read.c:5955 answers AVIF_RESULT_NOT_IMPLEMENTED when the alpha auxiliary item
    // carries a rotation the colour item lacks. Re-point the colour item's essential `irot`
    // association (0x85) in abc_color_irot_alpha_irot.avif at its own `pixi` (0x82) so only the
    // alpha item keeps the rotation.
    auto bytes = readFixture("abc_color_irot_alpha_irot.avif");
    const auto ipma = boxTypeOffset(bytes, "ipma");
    // type(4) + version/flags(4) + entry_count(4) + item_ID(2) + association_count(1) + 4 earlier
    // associations = the colour item's fifth association byte.
    const auto colourIrotAssociation = ipma + 19;
    REQUIRE(std::to_integer<std::uint8_t>(bytes[colourIrotAssociation]) == 0x85);
    bytes[colourIrotAssociation] = std::byte{0x82};

    pvdkit::avif::DecoderFactory factory;
    const auto created = factory.create(bytes, kOptions);
    REQUIRE_FALSE(created.has_value());
    CAPTURE(created.error().detail);
    CHECK(created.error().code == ErrorCode::UnsupportedFeature);
    CHECK(created.error().detail.find("mismatch") != std::string::npos);
}

TEST_CASE("animation timing is available for every sequence fixture")
{
    pvdkit::avif::DecoderFactory factory;
    constexpr std::array animations{
        "colors-animated-8bpc.avif",
        "colors-animated-8bpc-alpha-exif-xmp.avif",
        "colors-animated-12bpc-keyframes-0-2-3.avif",
    };
    for (const auto name : animations) {
        CAPTURE(name);
        auto bytes = readFixture(name);
        auto decoder = factory.create(bytes, kOptions);
        REQUIRE(decoder.has_value());
        for (std::uint32_t frame = 0; frame < (*decoder)->meta().frameCount; ++frame) {
            const auto timing = (*decoder)->frameTiming(frame);
            REQUIRE(timing.has_value());
            CHECK(timing->durationMs > 0);
        }
    }

    auto bytes = readFixture("anim_3frames.avif");
    auto decoder = factory.create(bytes, kOptions);
    REQUIRE(decoder.has_value());
    constexpr std::array<std::uint32_t, 3> expected{100, 200, 300};
    for (std::uint32_t frame = 0; frame < expected.size(); ++frame) {
        const auto timing = (*decoder)->frameTiming(frame);
        REQUIRE(timing.has_value());
        CHECK(timing->durationMs == expected[frame]);
    }
    const auto outOfRange = (*decoder)->frameTiming(3);
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().code == ErrorCode::PageOutOfRange);
    CHECK(outOfRange.error().detail == avifResultToString(AVIF_RESULT_NO_IMAGES_REMAINING));
}

TEST_CASE("every frame of the synthetic sequence decodes exactly, forwards and after a backward seek")
{
    auto bytes = readFixture("anim_3frames.avif");
    pvdkit::avif::DecoderFactory factory;
    auto decoder = factory.create(bytes, kOptions);
    REQUIRE(decoder.has_value());
    REQUIRE((*decoder)->meta().frameCount == 3);

    // SOURCES.md: frames are exactly red, green, blue (lossless gbrp), i.e. BGR below.
    constexpr std::array<Bgr, 3> expected{Bgr{0, 0, 255}, Bgr{0, 255, 0}, Bgr{255, 0, 0}};
    const auto checkFrame = [&](const std::uint32_t frame) {
        CAPTURE(frame);
        const auto decoded = decodeBgr(**decoder, frame);
        CHECK(decoded.at(0, 0) == expected[frame]);
        CHECK(decoded.at(32, 32) == expected[frame]);
        CHECK(decoded.at(63, 63) == expected[frame]);
    };

    checkFrame(0);
    checkFrame(1);
    checkFrame(2);
    // Backward seek: libavif must restart from the key frame rather than hand back frame 2 again.
    checkFrame(0);
    checkFrame(2);
    checkFrame(1);
}

TEST_CASE("lossless RGB fixture converts to exact caller-owned BGR pixels")
{
    const auto decoded = decodeFixtureBgr("quad_rgb_lossless.avif");
    CHECK(decoded.at(16, 16) == Bgr{0, 0, 255});
    CHECK(decoded.at(48, 16) == Bgr{0, 255, 0});
    CHECK(decoded.at(16, 48) == Bgr{255, 0, 0});
    CHECK(decoded.at(48, 48) == Bgr{255, 255, 255});
}

TEST_CASE("lossy limited-range 4:2:0 fixture converts to the generator's quadrant colours")
{
    const auto decoded = decodeFixtureBgr("quad_yuv420.avif");
    // The generator paints solid red, green, blue and white quadrants as limited-range BT.709
    // 4:2:0. SOURCES.md quotes ffmpeg's swscale conversion of the same coded data as RGB
    // (254,0,1), (0,253,0), (1,0,254), (255,255,255); libavif's libyuv path yields
    // (255,1,5), (1,255,11), (2,0,243), (255,255,255) - the two converters differ by up to 11 on
    // these saturated colours. A tolerance of 16 still rejects a BT.601 matrix (green would read
    // R = 20), a full-range misinterpretation (green G = 236) or swapped channels.
    constexpr int tolerance = 16;
    CHECK(within(decoded.at(16, 16), Bgr{0, 0, 255}, tolerance));
    CHECK(within(decoded.at(48, 16), Bgr{0, 255, 0}, tolerance));
    CHECK(within(decoded.at(16, 48), Bgr{255, 0, 0}, tolerance));
    CHECK(within(decoded.at(48, 48), Bgr{255, 255, 255}, tolerance));
}

TEST_CASE("monochrome 4:0:0 fixture decodes to the generator's exact full-range grey ramp")
{
    // make-synthetic-fixtures.ps1 renders `lum='4*X'` losslessly as full-range 4:0:0, so every
    // column x is the grey level 4x on all three channels.
    const auto decoded = decodeFixtureBgr("gray_400.avif");
    CHECK(decoded.width == 64);
    CHECK(decoded.height == 64);
    for (const std::uint32_t x : {0U, 1U, 17U, 32U, 50U, 63U}) {
        CAPTURE(x);
        const auto grey = static_cast<std::uint8_t>(4 * x);
        CHECK(decoded.at(x, 0) == Bgr{grey, grey, grey});
        CHECK(decoded.at(x, 63) == Bgr{grey, grey, grey});
    }
}

TEST_CASE("lossless 10-bit 4:4:4 fixture decodes to the generator's limited-range ramp within 2")
{
    // make-synthetic-fixtures.ps1 renders `lum='16*X'`, cb = cr = 512 (neutral chroma) as
    // limited-range yuv444p10le, so column x maps to the 8-bit grey level
    // round((16x - 64) / (940 - 64) * 255) clamped to [0, 255]; the tolerance covers the
    // difference between libavif's float and libyuv conversion paths.
    const auto decoded = decodeFixtureBgr("tenbit_444.avif");
    CHECK(decoded.width == 64);
    CHECK(decoded.height == 64);
    for (const std::uint32_t x : {0U, 4U, 5U, 20U, 32U, 45U, 58U, 59U, 63U}) {
        CAPTURE(x);
        const double normalized = (16.0 * x - 64.0) / 876.0;
        const auto grey = static_cast<std::uint8_t>(std::clamp(std::lround(normalized * 255.0), 0L, 255L));
        CHECK(within(decoded.at(x, 0), Bgr{grey, grey, grey}, 2));
        CHECK(within(decoded.at(x, 63), Bgr{grey, grey, grey}, 2));
    }
}

TEST_CASE("10-bit output uses the full 16-bit range instead of left-shifting source samples")
{
    // The source is limited-range YUV, so libavif first normalizes the 10-bit legal luma range
    // [64, 940], then rounds the full-range RGB result to [0, 65535].
    const auto decoded = decodeFixtureBgra64("tenbit_444.avif");
    CHECK(decoded.width == 64);
    CHECK(decoded.height == 64);
    for (const std::uint32_t x : {0U, 4U, 5U, 20U, 32U, 45U, 58U, 59U, 63U}) {
        CAPTURE(x);
        const float normalized = std::clamp((16.0F * static_cast<float>(x) - 64.0F) / 876.0F, 0.0F, 1.0F);
        const auto grey = static_cast<std::uint16_t>(std::lround(normalized * 65535.0F));
        CHECK(decoded.at(x, 0) == Bgra16{grey, grey, grey, 0xFFFF});
        CHECK(decoded.at(x, 63) == Bgra16{grey, grey, grey, 0xFFFF});
    }
}

TEST_CASE("12-bit straight alpha is rescaled over all 16 bits")
{
    const auto bytes = readFixture("colors-animated-12bpc-keyframes-0-2-3.avif");
    const auto source = decodeSourceAlpha(bytes, 0);
    REQUIRE(source.depth == 12);
    const auto decoded = decodeFixtureBgra64("colors-animated-12bpc-keyframes-0-2-3.avif");

    std::optional<std::pair<std::uint32_t, std::uint32_t>> coordinate;
    for (std::uint32_t y = 0; y < source.height && !coordinate; ++y) {
        for (std::uint32_t x = 0; x < source.width; ++x) {
            const auto sample = source.at(x, y);
            if (sample != 0 && sample != 4095 && rescaleTo16(sample, source.depth) != (sample << 4U)) {
                coordinate = std::pair{x, y};
                break;
            }
        }
    }
    REQUIRE(coordinate.has_value());
    const auto [x, y] = *coordinate;
    const auto sourceSample = source.at(x, y);
    const auto expected = rescaleTo16(sourceSample, source.depth);
    CAPTURE(x);
    CAPTURE(y);
    CAPTURE(sourceSample);
    CAPTURE(expected);
    CHECK(decoded.at(x, y)[3] == expected);
    CHECK(expected != (sourceSample << 4U));
}

TEST_CASE("10-bit P3/PQ and grid sources decode end-to-end through the adapter")
{
    const auto cosmos = decodeFixtureBgr("cosmos1650_yuv444_10bpc_p3pq.avif");
    CHECK(cosmos.width == 1024);
    CHECK(cosmos.height == 428);
    CHECK_FALSE(cosmos.uniform());

    const auto sofa = decodeFixtureBgr("sofa_grid1x5_420.avif");
    CHECK(sofa.width == 1024);
    CHECK(sofa.height == 770);
    CHECK_FALSE(sofa.uniform());
    // The five 1024x154 grid cells are stitched into one frame: the last row (inside the fifth
    // cell) must carry picture content too.
    CHECK_FALSE(sofa.rowUniform(769));
}

TEST_CASE("alpha fixture converts to exact straight BGRA alpha")
{
    auto bytes = readFixture("alpha_steps.avif");
    pvdkit::avif::DecoderFactory factory;
    auto decoder = factory.create(bytes, kOptions);
    REQUIRE(decoder.has_value());
    constexpr std::uint32_t pitch = 96 * 4;
    std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * 32);
    const auto result = (*decoder)->decodeFrame(0, PixelFormat::Bgra32, pixels, pitch);
    REQUIRE(result.has_value());
    CHECK(pixelAt<4>(pixels, pitch, 16, 16) == Bgra{255, 255, 255, 0});
    CHECK(pixelAt<4>(pixels, pitch, 48, 16) == Bgra{255, 255, 255, 128});
    CHECK(pixelAt<4>(pixels, pitch, 80, 16) == Bgra{255, 255, 255, 255});
}

TEST_CASE("decode validates capacity and reports an out-of-range frame as PageOutOfRange")
{
    auto bytes = readFixture("quad_rgb_lossless.avif");
    pvdkit::avif::DecoderFactory factory;
    auto decoder = factory.create(bytes, kOptions);
    REQUIRE(decoder.has_value());

    constexpr std::uint32_t pitch = 64 * 3;
    std::vector<std::byte> tooSmall(pitch * 64 - 1);
    const auto capacityError = (*decoder)->decodeFrame(0, PixelFormat::Bgr24, tooSmall, pitch);
    REQUIRE_FALSE(capacityError.has_value());
    CHECK(capacityError.error().code == ErrorCode::Internal);

    std::vector<std::byte> invalidPitch(64);
    const auto pitchError = (*decoder)->decodeFrame(0, PixelFormat::Bgr24, invalidPitch, 1);
    REQUIRE_FALSE(pitchError.has_value());
    CHECK(pitchError.error().code == ErrorCode::Internal);

    std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * 64);
    const auto outOfRange = (*decoder)->decodeFrame(1, PixelFormat::Bgr24, pixels, pitch);
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().code == ErrorCode::PageOutOfRange);
    CHECK(outOfRange.error().detail == avifResultToString(AVIF_RESULT_NO_IMAGES_REMAINING));
}

TEST_CASE("destination validation uses non-overflowing size arithmetic")
{
    using pvdkit::avif::detail::checkDestination;
    // width * 8 and pitch * height both exceed 32 bits; neither may wrap around.
    const pvdkit::core::ImageMeta hugeMeta{
        0x40000000U, 0x40000000U, 8, ChromaFormat::Yuv444, false, false, {}, 1, false, {}, false, false, false};
    const auto pitchOverflow = checkDestination(hugeMeta, PixelFormat::Bgra64, 0, 0);
    REQUIRE_FALSE(pitchOverflow.has_value());
    CHECK(pitchOverflow.error().code == ErrorCode::Internal);
    CHECK(pitchOverflow.error().detail == "caller pitch is smaller than one pixel row");

    // pitch * height = 3 * 2^60 is a multiple of 2^32: a wrapped product would accept 0xFFFFFFFF.
    const auto sizeOverflow = checkDestination(hugeMeta, PixelFormat::Bgr24, 0xFFFFFFFFU, 0xC0000000U);
    REQUIRE_FALSE(sizeOverflow.has_value());
    CHECK(sizeOverflow.error().code == ErrorCode::Internal);
    CHECK(sizeOverflow.error().detail == "caller buffer is smaller than pitch multiplied by height");

    const pvdkit::core::ImageMeta small{2,     3,     8,    ChromaFormat::Yuv444, true, false, {}, 1, false, {},
                                        false, false, false};
    CHECK(checkDestination(small, PixelFormat::Bgra32, 24, 8).has_value());
    CHECK(checkDestination(small, PixelFormat::Bgra32, 30, 10).has_value());
    CHECK(checkDestination(small, PixelFormat::Bgra64, 48, 16).has_value());
    CHECK(checkDestination(small, PixelFormat::Bgr24, 18, 6).has_value());
    CHECK_FALSE(checkDestination(small, PixelFormat::Bgra32, 23, 8).has_value());
    CHECK_FALSE(checkDestination(small, PixelFormat::Bgra32, 24, 7).has_value());
    CHECK_FALSE(checkDestination(small, PixelFormat::Bgra64, 48, 15).has_value());
    CHECK_FALSE(checkDestination(small, PixelFormat::Bgr24, 18, 5).has_value());
}

TEST_CASE("the RGB target hands libavif the caller buffer, requested BGR/BGRA depth and the thread budget")
{
    using pvdkit::avif::detail::rgbTarget;
    avifImage image{};
    image.width = 5;
    image.height = 2;
    image.depth = 10;
    std::array<std::byte, 5 * 8 * 2> buffer{};

    const avifRGBImage bgra = rgbTarget(image, PixelFormat::Bgra32, buffer, 20, 8);
    CHECK(bgra.width == 5);
    CHECK(bgra.height == 2);
    CHECK(bgra.depth == 8);
    CHECK(bgra.format == AVIF_RGB_FORMAT_BGRA);
    CHECK(bgra.alphaPremultiplied == AVIF_FALSE);
    CHECK(bgra.chromaUpsampling == AVIF_CHROMA_UPSAMPLING_AUTOMATIC);
    CHECK(bgra.maxThreads == 8);
    CHECK(bgra.pixels == reinterpret_cast<std::uint8_t *>(buffer.data()));
    CHECK(bgra.rowBytes == 20);

    const avifRGBImage bgr = rgbTarget(image, PixelFormat::Bgr24, buffer, 16, 1);
    CHECK(bgr.format == AVIF_RGB_FORMAT_BGR);
    CHECK(bgr.depth == 8);
    CHECK(bgr.maxThreads == 1);
    CHECK(bgr.rowBytes == 16);

    const avifRGBImage bgra64 = rgbTarget(image, PixelFormat::Bgra64, buffer, 40, 4);
    CHECK(bgra64.format == AVIF_RGB_FORMAT_BGRA);
    CHECK(bgra64.depth == 16);
    CHECK(bgra64.alphaPremultiplied == AVIF_FALSE);
    CHECK(bgra64.maxThreads == 4);
    CHECK(bgra64.pixels == reinterpret_cast<std::uint8_t *>(buffer.data()));
    CHECK(bgra64.rowBytes == 40);
}

TEST_CASE("signature probing and hostile inputs fail without crashing")
{
    pvdkit::avif::DecoderFactory factory;
    const std::array<std::byte, 11> shortInput{};
    CHECK_FALSE(factory.recognises({}));
    CHECK_FALSE(factory.recognises(shortInput));

    auto valid = readFixture("white_1x1.avif");
    CHECK(factory.recognises(valid));

    for (const auto name : {"garbage.bin", "not_avif.png", "not_avif.bmp"}) {
        CAPTURE(name);
        auto bytes = readFixture(name);
        CHECK_FALSE(factory.recognises(bytes));
        const auto result = factory.create(bytes, kOptions);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::ParseFailed);
        CHECK_FALSE(result.error().detail.empty());
    }
}

TEST_CASE("a file cut inside the meta box is rejected at parse time as truncated data")
{
    // truncated.avif is quad_yuv420.avif cut at 60% (211 bytes), inside the `ipco` box of `meta`.
    auto bytes = readFixture("truncated.avif");
    REQUIRE(bytes.size() == 211);
    pvdkit::avif::DecoderFactory factory;
    const auto truncated = factory.create(bytes, kOptions);
    REQUIRE_FALSE(truncated.has_value());
    CHECK(truncated.error().code == ErrorCode::ParseFailed);
    CHECK(truncated.error().detail.starts_with(avifResultToString(AVIF_RESULT_TRUNCATED_DATA)));
}

TEST_CASE("decode-time libavif failures surface through the same result mapping")
{
    auto bytes = readFixture("quad_rgb_lossless.avif");
    const auto payload = boxTypeOffset(bytes, "mdat") + 4;
    // The AV1 payload starts with the sequence header OBU (0x0a) and continues with the frame OBU
    // (0x32) eleven bytes later; the checks pin the layout the corruptions below rely on.
    REQUIRE(std::to_integer<std::uint8_t>(bytes[payload]) == 0x0a);
    REQUIRE(std::to_integer<std::uint8_t>(bytes[payload + 11]) == 0x32);
    pvdkit::avif::DecoderFactory factory;

    SUBCASE("a file cut inside mdat parses but fails to decode with truncated data")
    {
        std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(payload + 20));
        auto decoder = factory.create(cut, kOptions);
        CAPTURE(createError(decoder));
        REQUIRE(decoder.has_value());
        constexpr std::uint32_t pitch = 64 * 3;
        std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * 64);
        const auto decoded = (*decoder)->decodeFrame(0, PixelFormat::Bgr24, pixels, pitch);
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error().code == ErrorCode::ParseFailed);
        CHECK(decoded.error().detail.starts_with(avifResultToString(AVIF_RESULT_TRUNCATED_DATA)));
    }

    SUBCASE("a corrupted frame OBU fails to decode with DecodeFailed")
    {
        std::ranges::fill(std::span{bytes}.subspan(payload + 13), std::byte{0xff});
        auto decoder = factory.create(bytes, kOptions);
        CAPTURE(createError(decoder));
        REQUIRE(decoder.has_value());
        constexpr std::uint32_t pitch = 64 * 3;
        std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * 64);
        const auto decoded = (*decoder)->decodeFrame(0, PixelFormat::Bgr24, pixels, pitch);
        REQUIRE_FALSE(decoded.has_value());
        CAPTURE(decoded.error().detail);
        CHECK(decoded.error().code == ErrorCode::DecodeFailed);
        CHECK(decoded.error().detail.starts_with(avifResultToString(AVIF_RESULT_DECODE_COLOR_FAILED)));
    }
}

TEST_CASE("strict decoding rejects the legacy alpha item that lenient mode accepts")
{
    auto bytes = readFixture("alpha_noispe.avif");
    pvdkit::avif::DecoderFactory factory;
    const DecoderOptions lenient{2, false, kOptions.maxPixels, kOptions.maxDimension};
    const DecoderOptions strict{2, true, kOptions.maxPixels, kOptions.maxDimension};
    CHECK(factory.create(bytes, lenient).has_value());
    const auto strictResult = factory.create(bytes, strict);
    REQUIRE_FALSE(strictResult.has_value());
    CHECK(strictResult.error().code == ErrorCode::ParseFailed);
}

TEST_CASE("dimension and pixel limits map to TooLarge")
{
    auto bytes = readFixture("quad_rgb_lossless.avif");
    pvdkit::avif::DecoderFactory factory;

    const DecoderOptions dimensionLimit{1, false, kOptions.maxPixels, 63};
    const auto dimensionResult = factory.create(bytes, dimensionLimit);
    REQUIRE_FALSE(dimensionResult.has_value());
    CAPTURE(dimensionResult.error().detail);
    CHECK(dimensionResult.error().code == ErrorCode::TooLarge);

    const DecoderOptions pixelLimit{1, false, 4095, kOptions.maxDimension};
    const auto pixelResult = factory.create(bytes, pixelLimit);
    REQUIRE_FALSE(pixelResult.has_value());
    CAPTURE(pixelResult.error().detail);
    CHECK(pixelResult.error().code == ErrorCode::TooLarge);

    // A grid image reports the limit from the grid box (libavif read.c:2153, NOT_IMPLEMENTED) or
    // from the item ispe (read.c:5337); both carry "dimensions are too large" and map to TooLarge.
    auto gridBytes = readFixture("sofa_grid1x5_420.avif");
    const DecoderOptions gridLimit{1, false, kOptions.maxPixels, 1000};
    const auto gridResult = factory.create(gridBytes, gridLimit);
    REQUIRE_FALSE(gridResult.has_value());
    CAPTURE(gridResult.error().detail);
    CHECK(gridResult.error().code == ErrorCode::TooLarge);
}

TEST_CASE("pixel limits outside libavif's accepted range are clamped instead of failing create()")
{
    // libavif 1.4.2 read.c:5292-5296 answers AVIF_RESULT_NOT_IMPLEMENTED for imageSizeLimit == 0 or
    // > AVIF_DEFAULT_IMAGE_SIZE_LIMIT; core enforces the real maxPixels in PixelBuffer::create.
    pvdkit::avif::DecoderFactory factory;
    auto quad = readFixture("quad_rgb_lossless.avif");
    auto white = readFixture("white_1x1.avif");

    for (const std::uint64_t maxPixels : {kLibavifSizeLimit + 1, std::numeric_limits<std::uint64_t>::max()}) {
        CAPTURE(maxPixels);
        const DecoderOptions aboveRange{1, false, maxPixels, kOptions.maxDimension};
        auto created = factory.create(quad, aboveRange);
        CAPTURE(createError(created));
        REQUIRE(created.has_value());
        CHECK((*created)->meta().width == 64);
    }

    const DecoderOptions zero{1, false, 0, kOptions.maxDimension};
    auto onePixel = factory.create(white, zero);
    CAPTURE(createError(onePixel));
    REQUIRE(onePixel.has_value());
    CHECK((*onePixel)->meta().width == 1);

    const auto tooLarge = factory.create(quad, zero);
    REQUIRE_FALSE(tooLarge.has_value());
    CAPTURE(tooLarge.error().detail);
    CHECK(tooLarge.error().code == ErrorCode::TooLarge);
}

TEST_CASE("one and eight decoder threads produce identical pixels")
{
    CHECK(decodeFixtureBgr("quad_yuv420.avif", 1).pixels == decodeFixtureBgr("quad_yuv420.avif", 8).pixels);
    CHECK(decodeFixtureBgr("kodim03_yuv420_8bpc.avif", 1).pixels ==
          decodeFixtureBgr("kodim03_yuv420_8bpc.avif", 8).pixels);
}

TEST_CASE("every libavif result maps to one stable project error category")
{
    using Pair = std::pair<avifResult, ErrorCode>;
    constexpr std::array mappings{
        Pair{AVIF_RESULT_OK, ErrorCode::Internal},
        Pair{AVIF_RESULT_UNKNOWN_ERROR, ErrorCode::Internal},
        Pair{AVIF_RESULT_INVALID_FTYP, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_NO_CONTENT, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_NO_YUV_FORMAT_SELECTED, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_REFORMAT_FAILED, ErrorCode::ConversionFailed},
        Pair{AVIF_RESULT_UNSUPPORTED_DEPTH, ErrorCode::UnsupportedFeature},
        Pair{AVIF_RESULT_ENCODE_COLOR_FAILED, ErrorCode::Internal},
        Pair{AVIF_RESULT_ENCODE_ALPHA_FAILED, ErrorCode::Internal},
        Pair{AVIF_RESULT_BMFF_PARSE_FAILED, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_MISSING_IMAGE_ITEM, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_DECODE_COLOR_FAILED, ErrorCode::DecodeFailed},
        Pair{AVIF_RESULT_DECODE_ALPHA_FAILED, ErrorCode::DecodeFailed},
        Pair{AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_ISPE_SIZE_MISMATCH, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_NO_CODEC_AVAILABLE, ErrorCode::DecodeFailed},
        Pair{AVIF_RESULT_NO_IMAGES_REMAINING, ErrorCode::PageOutOfRange},
        Pair{AVIF_RESULT_INVALID_EXIF_PAYLOAD, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_INVALID_IMAGE_GRID, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION, ErrorCode::Internal},
        Pair{AVIF_RESULT_TRUNCATED_DATA, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_IO_NOT_SET, ErrorCode::Internal},
        Pair{AVIF_RESULT_IO_ERROR, ErrorCode::Internal},
        Pair{AVIF_RESULT_WAITING_ON_IO, ErrorCode::Internal},
        Pair{AVIF_RESULT_INVALID_ARGUMENT, ErrorCode::Internal},
        Pair{AVIF_RESULT_NOT_IMPLEMENTED, ErrorCode::UnsupportedFeature},
        Pair{AVIF_RESULT_OUT_OF_MEMORY, ErrorCode::Internal},
        Pair{AVIF_RESULT_CANNOT_CHANGE_SETTING, ErrorCode::Internal},
        Pair{AVIF_RESULT_INCOMPATIBLE_IMAGE, ErrorCode::Internal},
        Pair{AVIF_RESULT_INTERNAL_ERROR, ErrorCode::Internal},
        Pair{AVIF_RESULT_ENCODE_GAIN_MAP_FAILED, ErrorCode::Internal},
        Pair{AVIF_RESULT_DECODE_GAIN_MAP_FAILED, ErrorCode::DecodeFailed},
        Pair{AVIF_RESULT_INVALID_TONE_MAPPED_IMAGE, ErrorCode::ParseFailed},
        Pair{AVIF_RESULT_ENCODE_SAMPLE_TRANSFORM_FAILED, ErrorCode::Internal},
        Pair{AVIF_RESULT_DECODE_SAMPLE_TRANSFORM_FAILED, ErrorCode::DecodeFailed},
    };

    for (const auto &[result, expected] : mappings) {
        CAPTURE(result);
        CHECK(pvdkit::avif::errorCodeForResult(result) == expected);
    }
    CHECK(pvdkit::avif::errorCodeForResult(static_cast<avifResult>(999)) == ErrorCode::Internal);
}

TEST_CASE("library version text identifies all statically linked codecs")
{
    CHECK(pvdkit::avif::libraryVersions() == "libavif 1.4.2, dav1d 1.5.3, libyuv 1916");
}

TEST_CASE("metadata normalization covers invalid chroma and every transform branch")
{
    using namespace pvdkit::avif::detail;

    CHECK(chromaFormat(AVIF_PIXEL_FORMAT_YUV444).value() == ChromaFormat::Yuv444);
    CHECK(chromaFormat(AVIF_PIXEL_FORMAT_YUV422).value() == ChromaFormat::Yuv422);
    CHECK(chromaFormat(AVIF_PIXEL_FORMAT_YUV420).value() == ChromaFormat::Yuv420);
    CHECK(chromaFormat(AVIF_PIXEL_FORMAT_YUV400).value() == ChromaFormat::Yuv400);
    CHECK(chromaFormat(AVIF_PIXEL_FORMAT_NONE).error().code == ErrorCode::ParseFailed);
    CHECK(chromaFormat(AVIF_PIXEL_FORMAT_COUNT).error().code == ErrorCode::ParseFailed);
    CHECK(chromaFormat(static_cast<avifPixelFormat>(999)).error().code == ErrorCode::Internal);

    avifImage image{};
    image.width = 10;
    image.height = 10;
    avifDiagnostics diagnostics{};
    auto none = transforms(image, diagnostics);
    REQUIRE(none.has_value());
    CHECK_FALSE(none->clap.has_value());
    CHECK(none->irotAngle == 0);
    CHECK_FALSE(none->imir.has_value());

    image.transformFlags = AVIF_TRANSFORM_CLAP | AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR;
    image.clap.widthN = 8;
    image.clap.widthD = 1;
    image.clap.heightN = 8;
    image.clap.heightD = 1;
    image.clap.horizOffN = 0;
    image.clap.horizOffD = 1;
    image.clap.vertOffN = 0;
    image.clap.vertOffD = 1;
    image.irot.angle = 3;
    image.imir.axis = 0;
    auto topBottom = transforms(image, diagnostics);
    REQUIRE(topBottom.has_value());
    CHECK(topBottom->clap->x == 1);
    CHECK(topBottom->clap->y == 1);
    CHECK(topBottom->clap->width == 8);
    CHECK(topBottom->clap->height == 8);
    CHECK(topBottom->irotAngle == 3);
    CHECK(topBottom->imir == pvdkit::core::MirrorAxis::TopBottom);

    image.imir.axis = 1;
    auto leftRight = transforms(image, diagnostics);
    REQUIRE(leftRight.has_value());
    CHECK(leftRight->imir == pvdkit::core::MirrorAxis::LeftRight);

    image.clap.widthD = 0;
    const auto invalid = transforms(image, diagnostics);
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::InvalidTransform);
}

TEST_CASE("foreign result, allocation and timing helpers cover failure boundaries")
{
    avifDiagnostics diagnostics{};
    CHECK(pvdkit::avif::detail::checkedResult(AVIF_RESULT_OK, ErrorCode::Internal, diagnostics).has_value());
    const auto failed =
        pvdkit::avif::detail::checkedResult(AVIF_RESULT_REFORMAT_FAILED, ErrorCode::ConversionFailed, diagnostics);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::ConversionFailed);
    CHECK(failed.error().detail == avifResultToString(AVIF_RESULT_REFORMAT_FAILED));

    CHECK(pvdkit::avif::detail::durationMilliseconds(-1.0) == 0);
    CHECK(pvdkit::avif::detail::durationMilliseconds(0.0005) == 1);
    CHECK(pvdkit::avif::detail::durationMilliseconds(static_cast<double>(std::numeric_limits<std::uint32_t>::max())) ==
          std::numeric_limits<std::uint32_t>::max());
    // Between LONG_MAX (long is 32 bits on Windows, both architectures) and UINT32_MAX the value
    // still fits the uint32 frame time and must round, not overflow the rounding step.
    CHECK(pvdkit::avif::detail::durationMilliseconds(3000000.0004) == 3000000000U);
    CHECK(pvdkit::avif::detail::durationMilliseconds(4294967.2944) == std::numeric_limits<std::uint32_t>::max() - 1);

    pvdkit::avif::DecoderHandle empty;
    CHECK_THROWS_AS(static_cast<void>(pvdkit::avif::detail::requireDecoder(std::move(empty))), std::bad_alloc);
}
