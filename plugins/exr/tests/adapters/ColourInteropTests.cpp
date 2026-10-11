// colorInteropID resolves the RGB colour space even when chromaticities is absent.
// Equivalent Y/RY/BY samples must therefore use equivalent reconstruction weights.
// OpenEXR's RgbaOutputFile generates the YC samples; OutputFile::copyPixels preserves
// their compressed bytes while the test varies only colour attributes.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Imath/ImathVec.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <doctest/doctest.h>

#include "adapters/exr/Decoder.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"

namespace
{

    constexpr int kWidth = 32;
    constexpr int kHeight = 32;
    constexpr pvdkit::core::DecoderOptions kLegacyOptions{4, false, 16'384ULL * 16'384, 32'768, true, false};

    std::string utf8Path(const std::filesystem::path &path)
    {
        const auto utf8 = path.u8string();
        return {utf8.begin(), utf8.end()};
    }

    class ColourFiles
    {
      public:
        ColourFiles()
            : directory_(
                  std::filesystem::temp_directory_path() /
                  ("pvdkit-exr-colour-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            REQUIRE(std::filesystem::create_directory(directory_));
        }

        ~ColourFiles()
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory_, ignored);
        }

        ColourFiles(const ColourFiles &) = delete;
        ColourFiles &operator=(const ColourFiles &) = delete;
        ColourFiles(ColourFiles &&) = delete;
        ColourFiles &operator=(ColourFiles &&) = delete;

        [[nodiscard]] std::filesystem::path path(const std::string_view name) const
        {
            return directory_ / name;
        }

      private:
        std::filesystem::path directory_;
    };

    void writeYc(const std::filesystem::path &path, const Imf::Chromaticities &chromaticities,
                 const std::string_view interopID)
    {
        Imf::Header header{kWidth, kHeight};
        header.compression() = Imf::ZIP_COMPRESSION;
        header.insert("chromaticities", Imf::ChromaticitiesAttribute{chromaticities});
        header.insert("whiteLuminance", Imf::FloatAttribute{1000.0F});
        if (!interopID.empty()) {
            header.insert("colorInteropID", Imf::StringAttribute{std::string{interopID}});
        }
        // Zero-origin, fixed even dimensions keep the library's frame buffer wholly
        // inside this array, including the last scanline and subsampled chroma samples.
        std::array<Imf::Rgba, static_cast<std::size_t>(kWidth) * kHeight> pixels{};
        std::ranges::fill(pixels, Imf::Rgba{1.75F, 0.125F, 0.5F, 1.0F});
        const auto name = utf8Path(path);
        Imf::RgbaOutputFile output{name.c_str(), header, Imf::WRITE_YC, 1};
        output.setFrameBuffer(pixels.data(), 1, kWidth);
        output.writePixels(kHeight);
    }

    void copyWithColour(const std::filesystem::path &source, const std::filesystem::path &target,
                        const std::optional<Imf::Chromaticities> &chromaticities, const std::string_view interopID)
    {
        const auto sourceName = utf8Path(source);
        const auto targetName = utf8Path(target);
        Imf::InputFile input{sourceName.c_str(), 1};
        auto header = input.header();
        header.erase("chromaticities");
        header.erase("colorInteropID");
        if (chromaticities) {
            header.insert("chromaticities", Imf::ChromaticitiesAttribute{*chromaticities});
        }
        if (!interopID.empty()) {
            header.insert("colorInteropID", Imf::StringAttribute{std::string{interopID}});
        }
        Imf::OutputFile output{targetName.c_str(), header, 1};
        output.copyPixels(input);
    }

    struct DecodedColour
    {
        pvdkit::core::ImageMeta meta;
        std::vector<std::byte> pixels;
    };

    DecodedColour decodeColour(const std::filesystem::path &path)
    {
        const auto size = std::filesystem::file_size(path);
        REQUIRE(size > 0);
        REQUIRE(size <= std::numeric_limits<std::size_t>::max());
        REQUIRE(size <= static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()));
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        pvdkit::exr::DecoderFactory factory;
        auto decoder = factory.create(bytes, kLegacyOptions);
        REQUIRE(decoder.has_value());
        const auto &meta = (*decoder)->meta();
        REQUIRE(meta.width == kWidth);
        REQUIRE(meta.height == kHeight);
        DecodedColour image{meta, std::vector<std::byte>(static_cast<std::size_t>(kWidth) * kHeight * 8)};
        REQUIRE((*decoder)->decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, image.pixels, kWidth * 8).has_value());
        return image;
    }

    void checkEquivalent(const DecodedColour &actual, const DecodedColour &expected)
    {
        CHECK(actual.meta.cicp.primaries == expected.meta.cicp.primaries);
        CHECK(actual.meta.cicp.transfer == expected.meta.cicp.transfer);
        CHECK(actual.meta.chroma == pvdkit::core::ChromaFormat::Yuv420);
        CHECK_FALSE(actual.meta.hasAlpha);
        REQUIRE(actual.meta.masteringPeakNits.has_value());
        REQUIRE(expected.meta.masteringPeakNits.has_value());
        CHECK(*actual.meta.masteringPeakNits == *expected.meta.masteringPeakNits);
        CHECK(actual.meta.chromaticities.has_value() == expected.meta.chromaticities.has_value());
        if (expected.meta.chromaticities) {
            REQUIRE(actual.meta.chromaticities.has_value());
            const auto &a = *actual.meta.chromaticities;
            const auto &e = *expected.meta.chromaticities;
            CHECK(a.red.x == e.red.x);
            CHECK(a.red.y == e.red.y);
            CHECK(a.green.x == e.green.x);
            CHECK(a.green.y == e.green.y);
            CHECK(a.blue.x == e.blue.x);
            CHECK(a.blue.y == e.blue.y);
            CHECK(a.white.x == e.white.x);
            CHECK(a.white.y == e.white.y);
        }
        REQUIRE(actual.pixels.size() == expected.pixels.size());
        const auto differingBytes =
            std::inner_product(actual.pixels.begin(), actual.pixels.end(), expected.pixels.begin(), std::size_t{0},
                               std::plus{}, [](const auto a, const auto e) { return a != e ? 1U : 0U; });
        CHECK(differingBytes == 0);
    }

} // namespace

TEST_CASE("YC colorInteropID fallback reconstructs like equivalent explicit chromaticities")
{
    struct Space
    {
        std::string_view id;
        std::string_view description;
        std::uint16_t primaries;
        Imf::Chromaticities chromaticities;
    };
    const std::array spaces{
        Space{"lin_rec709", "linear Rec.709", 1, Imf::Chromaticities{}},
        Space{"lin_rec2020",
              "linear Rec.2020",
              9,
              {{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}}},
        Space{"lin_p3d65",
              "linear P3-D65",
              12,
              {{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}}},
        Space{"lin_ap0",
              "linear ACES AP0",
              2,
              {{0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}}},
        Space{"lin_ap1",
              "linear ACES AP1",
              2,
              {{0.713F, 0.293F}, {0.165F, 0.830F}, {0.128F, 0.044F}, {0.32168F, 0.33767F}}},
    };
    const ColourFiles files;
    for (const auto &space : spaces) {
        CAPTURE(space.id);
        const auto explicitPath = files.path("explicit.exr");
        const auto interopPath = files.path("interop.exr");
        writeYc(explicitPath, space.chromaticities, space.id);
        copyWithColour(explicitPath, interopPath, std::nullopt, space.id);
        const auto expected = decodeColour(explicitPath);
        const auto actual = decodeColour(interopPath);
        CHECK(actual.meta.cicp.primaries == space.primaries);
        CHECK(actual.meta.sourceDetail.find(space.description) != std::string::npos);
        CHECK(actual.meta.sourceDetail == expected.meta.sourceDetail);
        checkEquivalent(actual, expected);
    }
}

TEST_CASE("YC unknown or absent colorInteropID retains the Rec709 default")
{
    const ColourFiles files;
    const auto explicitPath = files.path("rec709.exr");
    writeYc(explicitPath, Imf::Chromaticities{}, "");
    const auto expected = decodeColour(explicitPath);
    for (const auto id : {"", "unknown", "lin_unrecognised"}) {
        CAPTURE(id);
        const auto defaultPath = files.path("default.exr");
        copyWithColour(explicitPath, defaultPath, std::nullopt, id);
        const auto actual = decodeColour(defaultPath);
        checkEquivalent(actual, expected);
        CHECK(actual.meta.sourceDetail.find("linear Rec.709") != std::string::npos);
    }
}

TEST_CASE("YC explicit chromaticities take precedence over known and unknown colorInteropID values")
{
    const std::array sets{
        Imf::Chromaticities{},
        // Both coordinates remain within the known-set matching tolerance; matching
        // metadata to Rec.2020 must still preserve the declared YC reconstruction weights.
        Imf::Chromaticities{{0.7088F, 0.2928F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}},
        Imf::Chromaticities{{0.620F, 0.340F}, {0.300F, 0.610F}, {0.140F, 0.070F}, {0.3127F, 0.3290F}},
    };
    const ColourFiles files;
    const auto explicitPath = files.path("explicit.exr");
    const auto conflictingPath = files.path("conflicting.exr");
    for (const auto &set : sets) {
        CAPTURE(set.red.x);
        writeYc(explicitPath, set, "");
        const auto expected = decodeColour(explicitPath);
        for (const auto id : {"lin_ap1", "unknown"}) {
            CAPTURE(id);
            copyWithColour(explicitPath, conflictingPath, set, id);
            const auto actual = decodeColour(conflictingPath);
            checkEquivalent(actual, expected);
            CHECK(actual.meta.sourceDetail == expected.meta.sourceDetail);
        }
    }
}

TEST_CASE("YC near-known chromaticities keep reconstruction weights finer than their coded metadata")
{
    const ColourFiles files;
    const auto explicitPath = files.path("near-rec2020.exr");
    const auto snappedPath = files.path("rec2020.exr");
    const Imf::Chromaticities nearRec2020{{0.7088F, 0.2928F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
    const Imf::Chromaticities rec2020{{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, {0.3127F, 0.3290F}};
    writeYc(explicitPath, nearRec2020, "lin_rec2020");
    copyWithColour(explicitPath, snappedPath, rec2020, "lin_rec2020");
    const auto actual = decodeColour(explicitPath);
    const auto snapped = decodeColour(snappedPath);
    CHECK(actual.meta.cicp.primaries == 9);
    CHECK(snapped.meta.cicp.primaries == 9);
    // The channel bytes are identical, but the exact declared sets have different
    // Y weights. This guards against reconstructing from only the matched CICP code.
    CHECK(actual.pixels != snapped.pixels);
}

TEST_CASE("YC unusable explicit chromaticities keep Rec709 instead of a colorInteropID fallback")
{
    const ColourFiles files;
    const auto explicitPath = files.path("rec709.exr");
    const auto unusablePath = files.path("unusable.exr");
    writeYc(explicitPath, Imf::Chromaticities{}, "");
    const auto expected = decodeColour(explicitPath);
    auto whiteY0 = Imf::Chromaticities{};
    whiteY0.white.y = 0;
    auto collinear = Imf::Chromaticities{};
    collinear.green = collinear.red;
    auto nonfinite = Imf::Chromaticities{};
    nonfinite.white.x = std::numeric_limits<float>::infinity();
    for (const auto &set : {whiteY0, collinear, nonfinite}) {
        CAPTURE(set.white.x);
        CAPTURE(set.white.y);
        CAPTURE(set.green.x);
        for (const auto id : {"", "lin_ap1", "unknown"}) {
            CAPTURE(id);
            copyWithColour(explicitPath, unusablePath, set, id);
            const auto actual = decodeColour(unusablePath);
            checkEquivalent(actual, expected);
            CHECK(actual.meta.sourceDetail == "half Y+chroma, linear Rec.709 assumed");
        }
    }
}
