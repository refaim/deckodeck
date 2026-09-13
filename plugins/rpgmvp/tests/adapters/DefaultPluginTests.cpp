#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "adapters/spng/Decoder.hpp"
#include "core/Error.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/PluginFactory.hpp"

namespace
{

    constexpr std::size_t kHostHeadSize = std::size_t{16} * 1024U;
    constexpr bool kDeepOutput = PVDKIT_TEST_DEEP_OUTPUT != 0;
    constexpr std::string_view kExperimentSuffix = " [experiment: deep output]";

    std::filesystem::path pluginFixturePath(const std::string_view name)
    {
        return std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
    }

    std::vector<std::byte> pluginFixture(const std::string_view name)
    {
        const auto path = pluginFixturePath(name);
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

TEST_CASE("the RPGMVP production composition reports its generated identity")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);
    const auto &info = plugin->info();
    CHECK(info.priority == pvdkit::pvd::kPluginIdentity.priority);
    CHECK(info.priority == 10);
    CHECK(info.name == pvdkit::pvd::kPluginIdentity.name);
    CHECK(info.name == "RPGMVP");
    CHECK(info.version == pvdkit::pvd::kPluginIdentity.version);
    CHECK(info.version == "1.0.0");
    auto expectedComments =
        "RPG Maker MV/MZ encrypted PNG decoder: " + pvdkit::rpgmvp::libraryVersions() + "; static build";
    if (kDeepOutput) {
        expectedComments += kExperimentSuffix;
    }
    CHECK(info.comments == expectedComments);
}

TEST_CASE("the RPGMVP production composition opens memory and disk inputs identically")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);
    const auto bytes = pluginFixture("indexed8_trns_82x38_shadow2.png_");
    const auto path = pluginFixturePath("indexed8_trns_82x38_shadow2.png_");
    const auto name = utf8(path);

    auto memory = plugin->open(pvdkit::pvd::OpenRequest{name, 0, bytes});
    REQUIRE(memory.has_value());
    CHECK((*memory)->imageInfo().pageCount == 1);
    CHECK_FALSE((*memory)->imageInfo().animated);
    CHECK((*memory)->imageInfo().formatName == "RPGMVP");
    CHECK((*memory)->imageInfo().compression == "Deflate");
    CHECK((*memory)->imageInfo().comments == "RPG Maker MV/MZ encrypted PNG, 8-bit palette with transparency");

    const auto head = std::span{bytes}.first(std::min(bytes.size(), kHostHeadSize));
    auto disk = plugin->open(pvdkit::pvd::OpenRequest{name, bytes.size(), head});
    REQUIRE(disk.has_value());
    CHECK((*disk)->imageInfo().comments == (*memory)->imageInfo().comments);
    auto page = (*disk)->pageInfo(0);
    REQUIRE(page.has_value());
    CHECK(page->width == 82);
    CHECK(page->height == 38);
    CHECK(page->bitsPerPixel == 8);
    CHECK(page->frameTimeMs == 0);
}

TEST_CASE("the RPGMVP production composition reports refusal, missing-file and limit errors")
{
    const auto plugin = pvdkit::pvd::makePlugin();
    REQUIRE(plugin != nullptr);

    const auto plain = pluginFixture("rgba8_48x48.png");
    auto refused = plugin->open(pvdkit::pvd::OpenRequest{"rgba8_48x48.png", 0, plain});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == pvdkit::core::ErrorCode::NotRecognised);

    const auto accepted = pluginFixture("rgba8_48x48.rpgmvp");
    auto missing =
        plugin->open(pvdkit::pvd::OpenRequest{utf8(pluginFixturePath("missing.rpgmvp")), accepted.size(), accepted});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == pvdkit::core::ErrorCode::FileOpenFailed);

    const auto huge = pluginFixture("too_large_100000x100000.rpgmvp");
    auto limited = plugin->open(pvdkit::pvd::OpenRequest{"too-large.rpgmvp", 0, huge});
    REQUIRE_FALSE(limited.has_value());
    CHECK(limited.error().code == pvdkit::core::ErrorCode::TooLarge);
}

TEST_CASE("the RPGMVP production composition accepts caller-supplied decoder limits")
{
    constexpr pvdkit::core::DecoderOptions options{1, false, std::uint64_t{4} * 1024U * 1024U, 47};
    const auto plugin = pvdkit::pvd::makePlugin(options);
    REQUIRE(plugin != nullptr);

    const auto bytes = pluginFixture("rgba8_48x48.rpgmvp");
    const auto opened = plugin->open(pvdkit::pvd::OpenRequest{"rgba8_48x48.rpgmvp", 0, bytes});
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code == pvdkit::core::ErrorCode::TooLarge);
}

TEST_CASE("caller-supplied deep output marks the experimental composition")
{
    constexpr pvdkit::core::DecoderOptions shallow{1, false, std::uint64_t{4} * 1024U * 1024U, 32'768, false};
    constexpr pvdkit::core::DecoderOptions deep{1, false, std::uint64_t{4} * 1024U * 1024U, 32'768, true};

    const auto shallowPlugin = pvdkit::pvd::makePlugin(shallow);
    const auto deepPlugin = pvdkit::pvd::makePlugin(deep);

    CHECK_FALSE(shallowPlugin->info().comments.ends_with(" [experiment: deep output]"));
    CHECK(deepPlugin->info().comments.ends_with(" [experiment: deep output]"));
}
