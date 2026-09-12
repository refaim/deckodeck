// Unit-level leak gate of the libavif adapter and the composed plugin, in-process (no DLL): a
// decoder created, used and destroyed N times on every fixture - and refused N times on every
// negative fixture - returns every heap block and every handle, dav1d's worker threads included
// (tests/support/LeakCheck.hpp).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "LeakCheck.hpp"
#include "adapters/avif/Decoder.hpp"
#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/Types.hpp"

namespace
{

    using pvdkit::core::DecoderOptions;
    using pvdkit::pvd::PixelFormat;
    using pvdkit::test::measureLeaks;
    using pvdkit::test::requireNoLeak;

    constexpr std::size_t kIterations = 200;
    constexpr DecoderOptions kOptions{4, false, std::uint64_t{16384} * 16384, 32768};

    struct Fixture
    {
        std::string name;
        std::vector<std::byte> bytes;
    };

    std::vector<Fixture> readFixtures()
    {
        std::vector<Fixture> fixtures;
        for (const auto &entry : std::filesystem::directory_iterator{std::filesystem::path{PVDKIT_FIXTURE_DIR}}) {
            if (!entry.is_regular_file() || entry.path().extension() == ".md") {
                continue;
            }
            Fixture fixture{entry.path().filename().string(), {}};
            fixture.bytes.resize(static_cast<std::size_t>(entry.file_size()));
            std::ifstream stream{entry.path(), std::ios::binary};
            REQUIRE(stream.good());
            stream.read(reinterpret_cast<char *>(fixture.bytes.data()),
                        static_cast<std::streamsize>(fixture.bytes.size()));
            REQUIRE(stream.good());
            fixtures.push_back(std::move(fixture));
        }
        std::ranges::sort(fixtures, {}, &Fixture::name);
        REQUIRE_FALSE(fixtures.empty());
        return fixtures;
    }

    // Splits the fixtures the way the factory sees them: parsed or refused.
    struct Split
    {
        std::vector<Fixture> parsed;
        std::vector<Fixture> refused;
    };

    Split split(std::vector<Fixture> fixtures)
    {
        pvdkit::avif::DecoderFactory factory;
        Split result;
        for (auto &fixture : fixtures) {
            const bool parsed =
                factory.recognises(fixture.bytes) && factory.create(fixture.bytes, kOptions).has_value();
            (parsed ? result.parsed : result.refused).push_back(std::move(fixture));
        }
        REQUIRE_FALSE(result.parsed.empty());
        REQUIRE_FALSE(result.refused.empty());
        return result;
    }

    const Fixture &cycle(const std::vector<Fixture> &fixtures, const std::size_t index)
    {
        return fixtures[index % fixtures.size()];
    }

} // namespace

TEST_CASE("leak: a decoder created, used and destroyed N times on every fixture returns everything")
{
    const auto fixtures = split(readFixtures());
    pvdkit::avif::DecoderFactory factory;
    const auto report = measureLeaks(
        "avif::Decoder create/decode/destroy", fixtures.parsed.size(), kIterations, [&](const std::size_t i) {
            const auto &fixture = cycle(fixtures.parsed, i);
            CAPTURE(fixture.name);
            REQUIRE(factory.recognises(fixture.bytes));
            auto decoder = factory.create(fixture.bytes, kOptions);
            REQUIRE(decoder.has_value());
            const auto &meta = (*decoder)->meta();
            const auto format = meta.hasAlpha ? PixelFormat::Bgra32 : PixelFormat::Bgr24;
            const std::uint32_t pitch = meta.width * (meta.hasAlpha ? 4U : 3U);
            std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * meta.height);
            for (std::uint32_t frame = 0; frame < meta.frameCount; ++frame) {
                REQUIRE((*decoder)->frameTiming(frame).has_value());
                REQUIRE((*decoder)->decodeFrame(frame, format, pixels, pitch).has_value());
            }
            // Error paths on a live decoder: a frame out of range and a destination too small.
            CHECK_FALSE((*decoder)->frameTiming(meta.frameCount).has_value());
            CHECK_FALSE((*decoder)->decodeFrame(meta.frameCount, format, pixels, pitch).has_value());
            CHECK_FALSE(
                (*decoder)->decodeFrame(0, format, std::span{pixels}.first(pixels.size() - 1), pitch).has_value());
        });
    requireNoLeak(report);
}

TEST_CASE("leak: a decoder refused N times on every negative fixture returns everything")
{
    const auto fixtures = split(readFixtures());
    pvdkit::avif::DecoderFactory factory;
    const auto report =
        measureLeaks("avif::DecoderFactory refusals", fixtures.refused.size(), kIterations, [&](const std::size_t i) {
            const auto &fixture = cycle(fixtures.refused, i);
            CAPTURE(fixture.name);
            const auto decoder = factory.create(fixture.bytes, kOptions);
            CHECK_FALSE(decoder.has_value());
            CHECK_FALSE(
                factory
                    .create(std::span{fixture.bytes}.first(std::min<std::size_t>(11, fixture.bytes.size())), kOptions)
                    .has_value());
        });
    requireNoLeak(report);
}

TEST_CASE("leak: the composed plugin created and destroyed N times returns everything")
{
    const auto report = measureLeaks("makePlugin create/destroy", 1, kIterations, [&](const std::size_t) {
        const auto plugin = pvdkit::pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        CHECK(plugin->info().priority == 10);
    });
    requireNoLeak(report);
}
