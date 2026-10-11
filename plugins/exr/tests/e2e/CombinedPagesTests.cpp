#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfStringVectorAttribute.h>
#include <doctest/doctest.h>

#include "PluginHost.hpp"
#include "RgbOutputChecks.hpp"

namespace
{
    // The library writes both part headers and chunks. Each physical part contains
    // two complete views; this differs from one view per part or a single multiview part.
    void writeCombinedInput(const std::filesystem::path &path)
    {
        std::array headers{Imf::Header{8, 8}, Imf::Header{8, 8}};
        std::array<Imf::FrameBuffer, 2> frames;
        std::array<std::array<std::vector<Imath::half>, 7>, 2> planes;
        constexpr std::array<std::array<float, 4>, 4> kColours{{{0.25F, 0.0F, 0.0F, 1.0F},
                                                                {0.0F, 0.25F, 0.0F, 0.5F},
                                                                {0.0F, 0.0F, 0.25F, 0.25F},
                                                                {0.25F, 0.25F, 0.0F, 1.0F}}};
        for (std::size_t part = 0; part < headers.size(); ++part) {
            auto &header = headers[part];
            header.setName("part" + std::to_string(part + 1));
            header.setType(Imf::SCANLINEIMAGE);
            header.insert("multiView", Imf::StringVectorAttribute{{"left", "right"}});
            std::size_t planeIndex = 0;
            for (std::size_t view = 0; view < 2; ++view) {
                const auto &colour = kColours[part * 2 + view];
                const auto channels = colour[3] < 1.0F ? 4U : 3U;
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    const std::string name =
                        std::string{view == 0 ? "beauty.left." : "beauty.right."} + "RGBA"[channel];
                    auto &plane = planes[part][planeIndex++];
                    const auto value = channel == 3 ? colour[3] : colour[channel] * colour[3];
                    plane.assign(64, Imath::half{value});
                    header.channels().insert(name, Imf::Channel{Imf::HALF});
                    frames[part].insert(name, Imf::Slice{Imf::HALF, reinterpret_cast<char *>(plane.data()),
                                                         sizeof(Imath::half), 8 * sizeof(Imath::half)});
                }
            }
        }
        Imf::MultiPartOutputFile output{path.string().c_str(), headers.data(), 2, false, 1};
        for (int part = 0; part < 2; ++part) {
            Imf::OutputPart image{output, part};
            image.setFrameBuffer(frames[static_cast<std::size_t>(part)]);
            image.writePixels(8);
        }
    }
} // namespace

TEST_CASE("EXR combined multipart multiview catalogue preserves four independent pages")
{
    using namespace pvdkit::e2e;
    const auto plugin = loadInitializedPlugin();
    const rgbChecks::PluginExit exit{plugin.exports()};
    const auto &exports = plugin.exports();
    const rgbChecks::FixtureDirectory directory;
    const auto path = std::filesystem::temp_directory_path() /
                      ("pvdkit-exr-combined-" + std::to_string(GetCurrentProcessId()) + ".exr");
    writeCombinedInput(path);
    REQUIRE(std::filesystem::is_regular_file(path));
    {
        const Imf::MultiPartInputFile input{path.string().c_str(), 1};
        REQUIRE(input.parts() == 2);
        for (int part = 0; part < input.parts(); ++part) {
            const auto &views = input.header(part).typedAttribute<Imf::StringVectorAttribute>("multiView").value();
            CHECK(views == Imf::StringVector({"left", "right"}));
        }
    }
    FixtureFile file;
    const auto utf8 = path.u8string();
    file.utf8Path.assign(utf8.begin(), utf8.end());
    file.bytes.resize(static_cast<std::size_t>(std::filesystem::file_size(path)));
    std::ifstream stream{path, std::ios::binary};
    stream.read(reinterpret_cast<char *>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
    REQUIRE(stream.good());
    stream.close();
    directory.isolate(file);
    REQUIRE(std::filesystem::remove(path));
    constexpr std::array<UINT32, 4> kWire{49, 65, 65, 49};
    constexpr std::array<UINT32, 4> kSourceBpp{48, 64, 64, 48};
    for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
        rgbChecks::Session native{exports, file, mode, true};
        rgbChecks::Session legacy{exports, file, mode, false};
        REQUIRE(native.image().info.nPages == 4);
        REQUIRE(legacy.image().info.nPages == 4);
        CHECK(native.image().info.Flags == 0);
        std::array<DecodedPage, 4> pages;
        std::array<std::vector<std::byte>, 4> retained;
        for (UINT32 index = 0; index < pages.size(); ++index) {
            auto &page = pages[index];
            page = rgbChecks::decodeRaw(exports, native.image().context, index);
            auto old = rgbChecks::decodeRaw(exports, legacy.image().context, index);
            CHECK(page.page.lWidth == 8);
            CHECK(page.page.lHeight == 8);
            CHECK(page.page.nBPP == kSourceBpp[index]);
            CHECK(page.page.lFrameTime == 0);
            CHECK(page.decode.nBPP == kWire[index]);
            CHECK(page.decode.Flags == ((index == 1 || index == 2) ? PVD_IDF_ALPHA : 0));
            rgbChecks::checkNativePage(page, old);
            const auto pixel = page.pixel(4, 4);
            constexpr std::array<std::array<bool, 3>, 4> kBright{
                {{true, false, false}, {false, true, false}, {false, false, true}, {true, true, false}}};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto sample = pixel[channel * 2] | (pixel[channel * 2 + 1] << 8U);
                CHECK((sample > 30'000) == kBright[index][channel]);
                CHECK((sample < 2'000) == !kBright[index][channel]);
            }
            if (index == 1 || index == 2) {
                const auto alpha = pixel[6] | (pixel[7] << 8U);
                CHECK(alpha == (index == 1 ? 32'768 : 16'384));
            }
            retained[index] = page.pixels();
            exports.pageFree(legacy.image().context, &old.decode);
        }
        for (UINT32 index = 0; index < pages.size(); ++index) {
            CHECK(pages[index].pixels() == retained[index]);
            for (UINT32 other = 0; other < index; ++other) {
                CHECK(pages[index].pixel(4, 4) != pages[other].pixel(4, 4));
            }
            auto revisited = rgbChecks::decodeRaw(exports, native.image().context, index);
            CHECK(revisited.pixels() == retained[index]);
            exports.pageFree(native.image().context, &revisited.decode);
        }
        pvdInfoPage invalid{};
        CHECK(exports.pageInfo(native.image().context, 4, &invalid) == FALSE);
        for (auto &page : pages) {
            exports.pageFree(native.image().context, &page.decode);
        }
    }
}
