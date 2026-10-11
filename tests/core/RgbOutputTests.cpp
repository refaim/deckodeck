#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <doctest/doctest.h>

#include "Fakes.hpp"
#include "core/CodecPlugin.hpp"
#include "core/FileSession.hpp"

namespace pvdkit::core
{

    TEST_CASE("RGB layout metadata separates PVD format codes from storage sizes")
    {
        constexpr std::array formats{pvd::PixelFormat::Bgr24, pvd::PixelFormat::Bgra32, pvd::PixelFormat::Bgra64,
                                     pvd::PixelFormat::Rgb24, pvd::PixelFormat::Rgba32, pvd::PixelFormat::Rgba64,
                                     pvd::PixelFormat::Rgb48};
        constexpr std::array<std::uint32_t, 7> bytes{3, 4, 8, 3, 4, 8, 6};
        constexpr std::array<std::uint32_t, 7> codes{24, 32, 64, 25, 33, 65, 49};
        for (std::size_t index = 0; index < formats.size(); ++index) {
            const auto layout = pvd::pixelLayout(formats[index]);
            CHECK(layout.bytesPerPixel == bytes[index]);
            CHECK(layout.pvdCode == codes[index]);
            CHECK(layout.rgb == (index >= 3));
        }
    }

    TEST_CASE("FileSession RGB policy preserves storage depth alpha and informational page depth")
    {
        for (const bool native : {false, true}) {
            for (const bool deep : {false, true}) {
                for (const bool presentation : {false, true}) {
                    for (const bool alpha : {false, true}) {
                        test::DecoderState state;
                        auto meta = test::meta(3, 2, alpha, deep ? 16 : 8);
                        if (presentation) {
                            meta.cicp = Cicp{9, 16, 9, false};
                        }
                        auto options = test::options();
                        options.deepOutput = true;
                        options.nativeRgbOutput = native;
                        FileSession session(nullptr, std::make_unique<test::FakeDecoder>(meta, state),
                                            test::imageInfo(meta), options, test::outputTables());
                        const auto decoded = session.decodePage(0, pvd::Progress{});
                        REQUIRE(decoded.has_value());
                        const auto bytes = deep || presentation ? (native && !alpha ? 6U : 8U) : (alpha ? 4U : 3U);
                        CHECK(decoded->bitsPerPixel == bytes * 8U + (native ? 1U : 0U));
                        CHECK(decoded->pitchBytes == 3U * bytes);
                        CHECK(decoded->pixels.size() == 6U * bytes);
                        CHECK(decoded->hasAlpha == alpha);
                        REQUIRE(state.decodedFormats.size() == 1);
                        CHECK(pvd::pixelLayout(state.decodedFormats[0]).rgb == native);
                        const auto page = session.pageInfo(0);
                        REQUIRE(page.has_value());
                        CHECK(page->bitsPerPixel == static_cast<std::uint32_t>(meta.depth) * (alpha ? 4U : 3U));
                        CHECK(session.freePage(decoded->pixels));
                    }
                }
            }
        }
    }

    TEST_CASE("CodecPlugin requires explicit RGB request and decoder opt in per session")
    {
        for (const bool optIn : {false, true}) {
            test::FileSourceState sourceState;
            test::FactoryState factoryState;
            test::DecoderState decoderState;
            test::DescriberState describerState;
            test::FakeFileSource source{sourceState};
            test::FakeDecoderFactory factory{factoryState, decoderState, test::meta()};
            test::FakeImageDescriber describer{describerState};
            auto options = test::options();
            options.nativeRgbOutput = optIn;
            CodecPlugin plugin(source, factory, describer, test::outputTables(), options,
                               pvd::PluginInfo{100, "test", "1", "test"});
            const std::array head{std::byte{1}};
            auto legacy = plugin.open(pvd::OpenRequest{"memory", 0, head, L"memory", false});
            auto native = plugin.open(pvd::OpenRequest{"memory", 0, head, {}, true});
            REQUIRE(legacy.has_value());
            REQUIRE(native.has_value());
            for (unsigned repeat = 0; repeat < 2; ++repeat) {
                const auto legacyPage = (*legacy)->decodePage(0, pvd::Progress{});
                const auto nativePage = (*native)->decodePage(0, pvd::Progress{});
                REQUIRE(legacyPage.has_value());
                REQUIRE(nativePage.has_value());
                CHECK(legacyPage->bitsPerPixel == 24);
                CHECK(nativePage->bitsPerPixel == (optIn ? 25U : 24U));
                CHECK((*legacy)->freePage(legacyPage->pixels));
                CHECK((*native)->freePage(nativePage->pixels));
            }
            CHECK(sourceState.openCalls == 0);
        }
    }

    TEST_CASE("RGB48 crop retains all six bytes and independent decoded page ownership")
    {
        test::DecoderState state;
        auto meta = test::meta(3, 2, false, 16);
        meta.transforms.clap = CropRect{1, 0, 1, 2};
        auto options = test::options();
        options.deepOutput = true;
        options.nativeRgbOutput = true;
        FileSession session(nullptr, std::make_unique<test::FakeDecoder>(meta, state), test::imageInfo(meta), options,
                            test::outputTables());
        const auto first = session.decodePage(0, pvd::Progress{});
        const auto second = session.decodePage(0, pvd::Progress{});
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(first->bitsPerPixel == 49);
        CHECK(first->pitchBytes == 6);
        REQUIRE(first->pixels.size() == 12);
        CHECK(first->pixels.data() != second->pixels.data());
        for (std::size_t channel = 0; channel < 6; ++channel) {
            CHECK(first->pixels[channel] == std::byte{static_cast<unsigned char>(2 + channel * 16)});
            CHECK(first->pixels[6 + channel] == std::byte{static_cast<unsigned char>(5 + channel * 16)});
        }
        CHECK(session.freePage(first->pixels));
        CHECK(second->pixels.front() == std::byte{2});
        CHECK(session.freePage(second->pixels));
    }

} // namespace pvdkit::core
