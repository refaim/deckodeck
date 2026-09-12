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

#include "adapters/avif/Decoder.hpp"
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
    CHECK(info.name == "AVIF");
    CHECK(info.version == pvdkit::pvd::kPluginIdentity.version);
    CHECK(info.version == "1.0.0");
    CHECK(info.comments == "AVIF decoder: " + pvdkit::avif::libraryVersions() + "; static build");
    CHECK(info.comments.find("libavif 1.4.2") != std::string::npos);
}

TEST_CASE("the production plugin opens a fixture from memory and from disk with identical results")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);
    const auto bytes = readFixture("anim_3frames.avif");
    const auto name = utf8(fixturePath("anim_3frames.avif"));

    auto fromMemory = plugin->open(pvdkit::pvd::OpenRequest{name, 0, bytes});
    REQUIRE(fromMemory.has_value());
    const auto &memoryInfo = (*fromMemory)->imageInfo();
    CHECK(memoryInfo.pageCount == 3);
    CHECK(memoryInfo.animated);
    CHECK(memoryInfo.formatName == "AVIF");
    CHECK(memoryInfo.compression == "AV1");
    CHECK(memoryInfo.comments.find("3 frames") != std::string::npos);

    const auto head = std::span{bytes}.first(std::min(bytes.size(), kHostHeadSize));
    auto fromDisk = plugin->open(pvdkit::pvd::OpenRequest{name, bytes.size(), head});
    REQUIRE(fromDisk.has_value());
    const auto &diskInfo = (*fromDisk)->imageInfo();
    CHECK(diskInfo.pageCount == memoryInfo.pageCount);
    CHECK(diskInfo.animated == memoryInfo.animated);
    CHECK(diskInfo.comments == memoryInfo.comments);

    const auto page = (*fromDisk)->pageInfo(2);
    REQUIRE(page.has_value());
    CHECK(page->width == 64);
    CHECK(page->height == 64);
    CHECK(page->frameTimeMs == 300);
}

TEST_CASE("the production plugin uses lenient decoding")
{
    // alpha_noispe.avif is accepted only with strict mode off (see DecoderTests).
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);
    const auto bytes = readFixture("alpha_noispe.avif");
    const auto opened = plugin->open(pvdkit::pvd::OpenRequest{"alpha_noispe.avif", 0, bytes});
    REQUIRE(opened.has_value());
    CHECK((*opened)->imageInfo().pageCount == 1);
}

TEST_CASE("the production plugin rejects non-AVIF input as NotRecognised and a missing file as FileOpenFailed")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);

    for (const auto name : {"garbage.bin", "not_avif.png", "not_avif.bmp"}) {
        CAPTURE(name);
        const auto bytes = readFixture(name);
        const auto opened = plugin->open(pvdkit::pvd::OpenRequest{name, 0, bytes});
        REQUIRE_FALSE(opened.has_value());
        CHECK(opened.error().code == ErrorCode::NotRecognised);
    }

    const auto bytes = readFixture("white_1x1.avif");
    const auto missing =
        plugin->open(pvdkit::pvd::OpenRequest{utf8(fixturePath("does-not-exist.avif")), bytes.size(), bytes});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileOpenFailed);
}
