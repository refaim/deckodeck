// Exception propagation through the real core entry points (AGENTS rule 5):
// core never catches, so an exception thrown by a dependency must leave
// CodecPlugin / FileSession unchanged and leak nothing.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "Fakes.hpp"
#include "core/CodecPlugin.hpp"
#include "core/FileSession.hpp"

namespace pvdkit::core
{
    namespace
    {

        pvd::PluginInfo pluginInfo()
        {
            return pvd::PluginInfo{10, "Fake", "1.1.0", "static decoder"};
        }

        std::unique_ptr<IDecoder> decoder(const ImageMeta &imageMeta, test::DecoderState &state)
        {
            return std::make_unique<test::FakeDecoder>(imageMeta, state);
        }

        std::vector<std::byte> bytesOf(const pvd::DecodedPage &page)
        {
            return {page.pixels.begin(), page.pixels.end()};
        }

        std::vector<unsigned> pixelIds(const pvd::DecodedPage &page, const std::uint32_t width,
                                       const std::uint32_t bytesPerPixel)
        {
            std::vector<unsigned> result;
            const auto height = static_cast<std::uint32_t>(page.pixels.size() / page.pitchBytes);
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    result.push_back(
                        std::to_integer<unsigned>(page.pixels[static_cast<std::size_t>(y) * page.pitchBytes +
                                                              static_cast<std::size_t>(x) * bytesPerPixel]));
                }
            }
            return result;
        }

        TEST_CASE("CodecPlugin::open propagates an exception from recognises "
                  "without opening or parsing")
        {
            test::FileSourceState fileState;
            test::FactoryState factoryState;
            factoryState.throwOnLook = true;
            test::DecoderState decoderState;
            test::FakeFileSource source(fileState);
            test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
            test::DescriberState describerState;
            test::FakeImageDescriber describer(describerState);
            CodecPlugin plugin(source, factory, describer, test::options(), pluginInfo());
            const std::vector head{std::byte{1}};

            CHECK_THROWS_AS(static_cast<void>(plugin.open(pvd::OpenRequest{"throw.avif", 1, head})),
                            test::SignatureException);

            CHECK(factoryState.lookCalls == 1);
            CHECK(fileState.openCalls == 0);
            CHECK(factoryState.createCalls == 0);
            CHECK(fileState.dataDestructions == 0);
            CHECK(decoderState.destructions == 0);
        }

        TEST_CASE("CodecPlugin::open propagates an exception from IFileSource::open "
                  "without creating a decoder")
        {
            test::FileSourceState fileState;
            fileState.throwOnOpen = true;
            test::FactoryState factoryState;
            test::DecoderState decoderState;
            test::FakeFileSource source(fileState);
            test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
            test::DescriberState describerState;
            test::FakeImageDescriber describer(describerState);
            CodecPlugin plugin(source, factory, describer, test::options(), pluginInfo());
            const std::vector head{std::byte{1}};

            CHECK_THROWS_AS(static_cast<void>(plugin.open(pvd::OpenRequest{"throw.avif", 1, head})),
                            test::FileOpenException);

            CHECK(factoryState.lookCalls == 1);
            CHECK(fileState.openCalls == 1);
            CHECK(factoryState.createCalls == 0);
            CHECK(fileState.dataDestructions == 0);
            CHECK(decoderState.destructions == 0);
        }

        TEST_CASE("CodecPlugin::open propagates an exception from "
                  "IDecoderFactory::create and releases the opened file data")
        {
            test::FileSourceState fileState;
            fileState.fileBytes = {std::byte{1}, std::byte{2}};
            test::FactoryState factoryState;
            factoryState.throwOnCreate = true;
            test::DecoderState decoderState;
            test::FakeFileSource source(fileState);
            test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
            test::DescriberState describerState;
            test::FakeImageDescriber describer(describerState);
            CodecPlugin plugin(source, factory, describer, test::options(), pluginInfo());
            const std::vector head{std::byte{1}};

            CHECK_THROWS_AS(static_cast<void>(plugin.open(pvd::OpenRequest{"throw.avif", 2, head})),
                            test::DecoderCreateException);

            CHECK(fileState.openCalls == 1);
            CHECK(factoryState.createCalls == 1);
            CHECK(factoryState.createdFrom == fileState.fileBytes);
            CHECK(fileState.dataDestructions == 1);
            CHECK(decoderState.destructions == 0);
        }

        TEST_CASE("CodecPlugin::open propagates an exception from "
                  "IImageDescriber::describe and releases the decoder and file data")
        {
            test::FileSourceState fileState;
            fileState.fileBytes = {std::byte{1}, std::byte{2}};
            test::FactoryState factoryState;
            test::DecoderState decoderState;
            test::FakeFileSource source(fileState);
            test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
            test::DescriberState describerState;
            describerState.throwOnDescribe = true;
            test::FakeImageDescriber describer(describerState);
            CodecPlugin plugin(source, factory, describer, test::options(), pluginInfo());
            const std::vector head{std::byte{1}};

            CHECK_THROWS_AS(static_cast<void>(plugin.open(pvd::OpenRequest{"throw.avif", 2, head})),
                            test::DescribeException);

            CHECK(factoryState.createCalls == 1);
            CHECK(describerState.describeCalls == 1);
            CHECK(fileState.dataDestructions == 1);
            CHECK(decoderState.destructions == 1);
        }

        TEST_CASE("FileSession::pageInfo propagates an exception from frameTiming and "
                  "keeps an outstanding page intact")
        {
            test::DecoderState state;
            int dataDestructions = 0;
            const auto imageMeta = test::meta(3, 2, false, 8, 2, true);
            {
                auto fileData =
                    std::make_unique<test::FakeFileData>(std::vector<std::byte>{std::byte{1}}, dataDestructions);
                FileSession session(std::move(fileData), decoder(imageMeta, state), test::imageInfo(imageMeta),
                                    test::options());
                const auto existing = session.decodePage(0, pvd::Progress{});
                REQUIRE(existing.has_value());
                const auto existingPixels = bytesOf(*existing);

                state.throwOnTiming = true;
                CHECK_THROWS_AS(static_cast<void>(session.pageInfo(1)), test::FrameTimingException);

                CHECK(state.timingFrames == std::vector<std::uint32_t>{1});
                CHECK(bytesOf(*existing) == existingPixels);
                CHECK(session.freePage(existing->pixels));
                CHECK_FALSE(session.freePage(existing->pixels));
                CHECK(dataDestructions == 0);
                CHECK(state.destructions == 0);
            }
            CHECK(dataDestructions == 1);
            CHECK(state.destructions == 1);
        }

        TEST_CASE("FileSession::decodePage propagates an exception from decodeFrame "
                  "without retaining the failed buffer")
        {
            test::DecoderState state;
            int dataDestructions = 0;
            const auto imageMeta = test::meta(3, 2, false, 8, 2, true);
            {
                auto fileData =
                    std::make_unique<test::FakeFileData>(std::vector<std::byte>{std::byte{1}}, dataDestructions);
                FileSession session(std::move(fileData), decoder(imageMeta, state), test::imageInfo(imageMeta),
                                    test::options());
                const auto existing = session.decodePage(0, pvd::Progress{});
                REQUIRE(existing.has_value());
                const auto existingPixels = bytesOf(*existing);

                state.throwOnDecode = true;
                CHECK_THROWS_AS(static_cast<void>(session.decodePage(1, pvd::Progress{})), test::DecodeFrameException);
                state.throwOnDecode = false;

                CHECK(state.decodedFrames == std::vector<std::uint32_t>{0, 1});
                CHECK(bytesOf(*existing) == existingPixels);

                // The session is still fully usable: only the pages it returned are
                // outstanding, each freeable exactly once.
                const auto fresh = session.decodePage(1, pvd::Progress{});
                REQUIRE(fresh.has_value());
                CHECK(pixelIds(*fresh, 3, 3) == std::vector<unsigned>{101, 102, 103, 104, 105, 106});
                CHECK(fresh->pixels.data() != existing->pixels.data());
                CHECK(session.freePage(fresh->pixels));
                CHECK_FALSE(session.freePage(fresh->pixels));
                CHECK(session.freePage(existing->pixels));
                CHECK_FALSE(session.freePage(existing->pixels));
                CHECK(dataDestructions == 0);
                CHECK(state.destructions == 0);
            }
            CHECK(dataDestructions == 1);
            CHECK(state.destructions == 1);
        }

        TEST_CASE("FileSession::decodePage propagates an exception from the Progress "
                  "callback at every step without retaining the in-progress buffer")
        {
            for (std::uint32_t throwStep = 0; throwStep < 3; ++throwStep) {
                test::DecoderState state;
                int dataDestructions = 0;
                const auto imageMeta = test::meta(3, 2, false, 8, 2, true);
                {
                    auto fileData =
                        std::make_unique<test::FakeFileData>(std::vector<std::byte>{std::byte{1}}, dataDestructions);
                    FileSession session(std::move(fileData), decoder(imageMeta, state), test::imageInfo(imageMeta),
                                        test::options());
                    const auto existing = session.decodePage(0, pvd::Progress{});
                    REQUIRE(existing.has_value());
                    const auto existingPixels = bytesOf(*existing);
                    std::vector<std::uint32_t> reports;
                    const pvd::Progress throwingProgress{[&](const std::uint32_t step, const std::uint32_t steps) {
                        CHECK(steps == 3);
                        reports.push_back(step);
                        if (step == throwStep) {
                            throw std::logic_error("scripted progress exception");
                        }
                        return true;
                    }};

                    CHECK_THROWS_AS(static_cast<void>(session.decodePage(1, throwingProgress)), std::logic_error);

                    CHECK(reports.back() == throwStep);
                    CHECK(state.decodedFrames.size() == (throwStep == 0 ? 1 : 2));
                    CHECK(bytesOf(*existing) == existingPixels);

                    const auto fresh = session.decodePage(1, pvd::Progress{});
                    REQUIRE(fresh.has_value());
                    CHECK(pixelIds(*fresh, 3, 3) == std::vector<unsigned>{101, 102, 103, 104, 105, 106});
                    CHECK(session.freePage(fresh->pixels));
                    CHECK_FALSE(session.freePage(fresh->pixels));
                    CHECK(session.freePage(existing->pixels));
                    CHECK_FALSE(session.freePage(existing->pixels));
                    CHECK(dataDestructions == 0);
                    CHECK(state.destructions == 0);
                }
                CHECK(dataDestructions == 1);
                CHECK(state.destructions == 1);
            }
        }

    } // namespace
} // namespace pvdkit::core
