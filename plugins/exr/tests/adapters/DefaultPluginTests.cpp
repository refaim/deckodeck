#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "adapters/exr/Context.hpp"
#include "core/Error.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/PluginFactory.hpp"

namespace
{

    using pvdkit::core::ErrorCode;

    constexpr std::size_t kHostHeadSize = 16 * 1024;

    std::filesystem::path fixturePath(const std::string_view name)
    {
        return std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
    }

    std::vector<std::byte> readFixture(const std::string_view name)
    {
        const auto path = fixturePath(name);
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        return bytes;
    }

    std::string utf8(const std::filesystem::path &path)
    {
        const auto text = path.u8string();
        return {text.begin(), text.end()};
    }

} // namespace

TEST_CASE("the production plugin reports the generated plugin identity and the linked library versions")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);

    const auto &info = plugin->info();
    CHECK(info.priority == pvdkit::pvd::kPluginIdentity.priority);
    CHECK(info.priority == 10);
    CHECK(info.name == pvdkit::pvd::kPluginIdentity.name);
    CHECK(info.name == "EXR");
    CHECK(info.version == pvdkit::pvd::kPluginIdentity.version);
    CHECK(info.version == "1.0.0");
    CHECK(info.comments == "OpenEXR decoder: " + pvdkit::exr::libraryVersions() + "; static build");
    CHECK(info.comments.find("OpenEXR 3.4.13") != std::string::npos);
}

TEST_CASE("the production plugin opens a fixture from memory and from disk with identical results")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);
    const auto bytes = readFixture("rgb_half_zip.exr");
    const auto name = utf8(fixturePath("rgb_half_zip.exr"));

    auto fromMemory = plugin->open(pvdkit::pvd::OpenRequest{name, 0, bytes});
    REQUIRE(fromMemory.has_value());
    const auto &memoryInfo = (*fromMemory)->imageInfo();
    CHECK(memoryInfo.pageCount == 1);
    CHECK_FALSE(memoryInfo.animated);
    CHECK(memoryInfo.formatName == "OpenEXR");
    CHECK(memoryInfo.compression == "ZIP");
    CHECK(memoryInfo.comments ==
          "half RGB, linear Rec.709, ZIP, scanline, display 16x8, data 16x8 at 0,0, 100 nit white, "
          "→ sRGB (BT.2390 tone map from 497 nit)");

    const auto head = std::span{bytes}.first(std::min(bytes.size(), kHostHeadSize));
    auto fromDisk = plugin->open(pvdkit::pvd::OpenRequest{name, bytes.size(), head});
    REQUIRE(fromDisk.has_value());
    const auto &diskInfo = (*fromDisk)->imageInfo();
    CHECK(diskInfo.pageCount == memoryInfo.pageCount);
    CHECK(diskInfo.compression == memoryInfo.compression);
    CHECK(diskInfo.comments == memoryInfo.comments);

    const auto page = (*fromDisk)->pageInfo(0);
    REQUIRE(page.has_value());
    CHECK(page->width == 16);
    CHECK(page->height == 8);
    CHECK(page->bitsPerPixel == 48); // 16-bit half x 3 channels, informational
    CHECK(page->frameTimeMs == 0);

    // Every EXR page is presented as BGRA64 through the colour path, alpha or not.
    const auto decoded = (*fromDisk)->decodePage(0, pvdkit::pvd::Progress{});
    REQUIRE(decoded.has_value());
    CHECK(decoded->bitsPerPixel == 64);
    CHECK(decoded->pitchBytes == 16 * 8);
    CHECK_FALSE(decoded->hasAlpha);
    CHECK((*fromDisk)->freePage(decoded->pixels));
}

TEST_CASE("the EXR production composition accepts caller-supplied decoder limits")
{
    constexpr pvdkit::core::DecoderOptions options{1, false, 100, 8, true};
    const auto plugin = pvdkit::pvd::makePlugin(options);
    REQUIRE(plugin != nullptr);

    const auto bytes = readFixture("rgb_half_zip.exr");
    const auto opened = plugin->open(pvdkit::pvd::OpenRequest{"rgb_half_zip.exr", 0, bytes});
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code == ErrorCode::TooLarge);
}

TEST_CASE("the production plugin rejects non-EXR input as NotRecognised and a missing file as FileOpenFailed")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);

    for (const auto name : {"garbage.bin", "not_exr.txt"}) {
        CAPTURE(name);
        const auto bytes = readFixture(name);
        const auto opened = plugin->open(pvdkit::pvd::OpenRequest{name, 0, bytes});
        REQUIRE_FALSE(opened.has_value());
        CHECK(opened.error().code == ErrorCode::NotRecognised);
    }

    const auto bytes = readFixture("rgb_half_zip.exr");
    const auto missing =
        plugin->open(pvdkit::pvd::OpenRequest{utf8(fixturePath("does-not-exist.exr")), bytes.size(), bytes});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileOpenFailed);

    // Recognised by its magic number, refused by the parser.
    const auto magic = readFixture("magic_only.exr");
    const auto refused = plugin->open(pvdkit::pvd::OpenRequest{"magic_only.exr", 0, magic});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::ParseFailed);
}
