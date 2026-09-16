// The luminance-chroma reconstruction against the library's own: Imf::RgbaInputFile (the C++
// layer, linked into this test only) reconstructs RGB from Y/RY/BY with the same RgbaYca filters
// the adapter drives through the Core, so the two must agree on every pixel of every Y/RY/BY
// fixture - the adapter's orchestration of the 27-row window, the edge handling and the saturation
// fix are what this test proves. The values are compared as the PQ codes the adapter hands the
// core (the library's half output, converted with the test's double-precision reference).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Imath/ImathBox.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfRgbaYca.h>
#include <OpenEXR/ImfStdIO.h>
#include <doctest/doctest.h>

#include "adapters/exr/Decoder.hpp"
#include "adapters/exr/LuminanceChroma.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"
#include "support/ReferencePipeline.hpp"

namespace
{

    using pvdkit::core::DecoderOptions;
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

    struct LibraryImage
    {
        int width = 0;
        int height = 0;
        std::vector<Imf::Rgba> pixels;
        std::array<double, 8> chromaticities{0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};

        [[nodiscard]] const Imf::Rgba &at(const int x, const int y) const
        {
            return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
        }
    };

    // The C++ library's reading of the file: RgbaInputFile reconstructs Y/RY/BY through RgbaYca.
    LibraryImage readWithLibrary(const std::string_view name)
    {
        const auto path = (std::filesystem::path{PVDKIT_FIXTURE_DIR} / name).string();
        Imf::RgbaInputFile file{path.c_str(), 1};
        const auto window = file.dataWindow();
        LibraryImage image;
        image.width = window.max.x - window.min.x + 1;
        image.height = window.max.y - window.min.y + 1;
        image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
        file.setFrameBuffer(image.pixels.data() - window.min.x -
                                static_cast<std::ptrdiff_t>(window.min.y) * image.width,
                            1, static_cast<std::size_t>(image.width));
        file.readPixels(window.min.y, window.max.y);
        if (const auto *set = file.header().findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities")) {
            const auto &c = set->value();
            image.chromaticities = {c.red.x, c.red.y, c.green.x, c.green.y, c.blue.x, c.blue.y, c.white.x, c.white.y};
        }
        return image;
    }

    struct PluginImage
    {
        std::uint32_t width;
        std::uint32_t height;
        std::vector<std::byte> pixels;

        [[nodiscard]] std::uint16_t sample(const std::uint32_t x, const std::uint32_t y,
                                           const std::size_t channel) const
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 8 + channel * 2;
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(pixels[offset]) |
                                              (std::to_integer<std::uint8_t>(pixels[offset + 1]) << 8U));
        }
    };

    PluginImage readWithPlugin(const std::string_view name)
    {
        const auto bytes = readFixture(name);
        pvdkit::exr::DecoderFactory factory;
        auto decoder = factory.create(bytes, kOptions);
        REQUIRE(decoder.has_value());
        const auto &meta = (*decoder)->meta();
        PluginImage image{meta.width, meta.height,
                          std::vector<std::byte>(static_cast<std::size_t>(meta.width) * meta.height * 8)};
        REQUIRE((*decoder)->decodeFrame(0, PixelFormat::Bgra64, image.pixels, meta.width * 8).has_value());
        return image;
    }

} // namespace

TEST_CASE("luminance-chroma files decode to the same RGB the library's RgbaInputFile reconstructs")
{
    // chroma_zips.exr has one line per chunk (odd chunks carry no chroma row), chroma_extra_channel.exr
    // an unselected Z channel the assignment must skip.
    for (const auto name : {"Rec709_YC.exr", "XYZ_YC.exr", "chroma_zips.exr", "chroma_extra_channel.exr"}) {
        // (yc_whitey0.exr and yc_collinear.exr have no library reading: RgbaInputFile throws on
        // their chromaticities; the e2e test pins them to chroma_extra_channel.exr instead.)
        CAPTURE(std::string_view{name});
        const auto library = readWithLibrary(name);
        const auto plugin = readWithPlugin(name);
        REQUIRE(plugin.width == static_cast<std::uint32_t>(library.width));
        REQUIRE(plugin.height == static_cast<std::uint32_t>(library.height));

        // Every pixel, as the PQ codes of the library's half RGB at 100 nit per unit. Half has
        // 11 significant bits, the PQ code 16: one code of slack covers the rounding of the two paths.
        std::size_t compared = 0;
        int worst = 0;
        for (int y = 0; y < library.height; ++y) {
            for (int x = 0; x < library.width; ++x) {
                const auto &rgba = library.at(x, y);
                const std::array<double, 3> nits{pvdkit::exr::tests::sanitized(static_cast<float>(rgba.r)) * 100.0,
                                                 pvdkit::exr::tests::sanitized(static_cast<float>(rgba.g)) * 100.0,
                                                 pvdkit::exr::tests::sanitized(static_cast<float>(rgba.b)) * 100.0};
                const std::array<std::uint16_t, 3> expected{
                    pvdkit::exr::tests::code16(pvdkit::exr::tests::pqEncode(nits[2])),
                    pvdkit::exr::tests::code16(pvdkit::exr::tests::pqEncode(nits[1])),
                    pvdkit::exr::tests::code16(pvdkit::exr::tests::pqEncode(nits[0]))};
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const auto actual =
                        plugin.sample(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), channel);
                    worst = std::max(worst, std::abs(static_cast<int>(actual) - static_cast<int>(expected[channel])));
                }
                CHECK(plugin.sample(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 3) == 65'535);
                ++compared;
            }
        }
        CAPTURE(worst);
        CHECK(worst <= 1);
        CHECK(compared == static_cast<std::size_t>(library.width) * static_cast<std::size_t>(library.height));
    }
}

TEST_CASE("the XYZ luminance-chroma file reports its chromaticities as the CIE XYZ set")
{
    const auto bytes = readFixture("XYZ_YC.exr");
    pvdkit::exr::DecoderFactory factory;
    const auto decoder = factory.create(bytes, kOptions);
    REQUIRE(decoder.has_value());
    const auto &meta = (*decoder)->meta();
    CHECK(meta.cicp.primaries == 2);
    REQUIRE(meta.chromaticities.has_value());
    CHECK(meta.chromaticities->red.x == doctest::Approx(1.0F));
    CHECK(meta.chromaticities->white.x == doctest::Approx(1.0F / 3.0F));
    CHECK(meta.sourceDetail.find("linear CIE XYZ") != std::string::npos);
    const auto library = readWithLibrary("XYZ_YC.exr");
    CHECK(library.chromaticities[0] == doctest::Approx(1.0));
}

TEST_CASE("the reconstruction weights are the library's bits for the sets it accepts")
{
    // RgbaInputFile reconstructs with RgbaYca::computeYw; the adapter spells that arithmetic out
    // (no library call that can throw) and must land on the same floats, or the halves the
    // reconstruction rounds would differ from the library's at rounding boundaries.
    using pvdkit::core::colour::Primaries::Chromaticities;
    constexpr Chromaticities rec709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.3290F}};
    constexpr Chromaticities xyz{{1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F / 3.0F, 1.0F / 3.0F}};
    constexpr Chromaticities ap0{{0.7347F, 0.2653F}, {0.0F, 1.0F}, {0.0001F, -0.0770F}, {0.32168F, 0.33767F}};
    for (const auto &set : {rec709, xyz, ap0}) {
        CAPTURE(set.red.x);
        const auto ours = pvdkit::exr::luminanceWeights(set);
        const auto library = Imf::RgbaYca::computeYw(
            Imf::Chromaticities{Imath::V2f{set.red.x, set.red.y}, Imath::V2f{set.green.x, set.green.y},
                                Imath::V2f{set.blue.x, set.blue.y}, Imath::V2f{set.white.x, set.white.y}});
        CHECK(ours.x == library.x);
        CHECK(ours.y == library.y);
        CHECK(ours.z == library.z);
    }
    const auto absent = pvdkit::exr::luminanceWeights(std::nullopt);
    const auto libraryDefault = Imf::RgbaYca::computeYw(Imf::Chromaticities{});
    CHECK(absent.x == libraryDefault.x);
    CHECK(absent.y == libraryDefault.y);
    CHECK(absent.z == libraryDefault.z);
}
