#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "Fakes.hpp"
#include "core/CodecPlugin.hpp"
#include "core/FileSession.hpp"
#include "core/colour/Pipeline.hpp"

namespace pvdkit::core
{
    namespace
    {

        pvd::PluginInfo pluginInfo()
        {
            return pvd::PluginInfo{10, "Fake", "1.1.0", "static decoder"};
        }

        // The collaborators every test wires into a CodecPlugin, declared in dependency order so the
        // plugin (last) is destroyed first.
        struct Harness
        {
            test::FileSourceState fileState;
            test::FactoryState factoryState;
            test::DecoderState decoderState;
            test::DescriberState describerState;
            test::FakeFileSource source{fileState};
            test::FakeDecoderFactory factory;
            test::FakeImageDescriber describer{describerState};
            // Heap-held: 384 KiB does not belong on the test stack.
            std::unique_ptr<const colour::SrgbOutputTables> outputTables =
                std::make_unique<const colour::SrgbOutputTables>();
            CodecPlugin plugin;

            explicit Harness(ImageMeta imageMeta = test::meta(), const DecoderOptions options = test::options())
                : factory(factoryState, decoderState, imageMeta),
                  plugin(source, factory, describer, *outputTables, options, pluginInfo())
            {
            }
        };

        TEST_CASE("CodecPlugin returns its stored plugin information")
        {
            Harness harness;

            CHECK(harness.plugin.info().priority == 10);
            CHECK(harness.plugin.info().name == "Fake");
            CHECK(harness.plugin.info().version == "1.1.0");
            CHECK(harness.plugin.info().comments == "static decoder");
        }

        TEST_CASE("CodecPlugin rejects a failed signature before opening, parsing or "
                  "describing")
        {
            Harness harness;
            harness.factoryState.recognises = false;
            const std::vector head{std::byte{1}, std::byte{2}};

            const auto opened = harness.plugin.open(pvd::OpenRequest{"ignored.bin", 100, head});

            REQUIRE_FALSE(opened.has_value());
            CHECK(opened.error().code == ErrorCode::NotRecognised);
            CHECK(harness.factoryState.lookedAt == head);
            CHECK(harness.factoryState.createCalls == 0);
            CHECK(harness.fileState.openCalls == 0);
            CHECK(harness.describerState.describeCalls == 0);
        }

        TEST_CASE("CodecPlugin memory mode parses the whole head without using the "
                  "file source")
        {
            auto imageMeta = test::meta(7, 5, true, 10, 3, true);
            imageMeta.hasIcc = true;
            Harness harness(imageMeta, test::options(77));
            const std::vector wholeFile{std::byte{3}, std::byte{4}, std::byte{5}};

            auto opened = harness.plugin.open(pvd::OpenRequest{"archive-entry.bin", 0, wholeFile});

            REQUIRE(opened.has_value());
            CHECK(harness.fileState.openCalls == 0);
            CHECK(harness.factoryState.createCalls == 1);
            CHECK(harness.factoryState.createdFrom == wholeFile);
            CHECK(harness.factoryState.receivedOptions.maxThreads == 2);
            CHECK(harness.factoryState.receivedOptions.maxPixels == 77);
            CHECK((*opened)->imageInfo().pageCount == 3);
            CHECK((*opened)->imageInfo().animated);
        }

        TEST_CASE("CodecPlugin builds the image information from the decoder's "
                  "metadata and the injected describer")
        {
            auto imageMeta = test::meta(7, 5, true, 10, 3, true);
            imageMeta.hasXmp = true;
            Harness harness(imageMeta);
            harness.describerState.description =
                ImageDescription{"Format name", "Codec name", "described by the plugin"};
            const std::vector wholeFile{std::byte{3}};

            auto opened = harness.plugin.open(pvd::OpenRequest{"a.bin", 0, wholeFile});

            REQUIRE(opened.has_value());
            CHECK(harness.describerState.describeCalls == 1);
            REQUIRE(harness.describerState.describedMeta.has_value());
            CHECK(harness.describerState.describedMeta->width == 7);
            CHECK(harness.describerState.describedMeta->height == 5);
            CHECK(harness.describerState.describedMeta->depth == 10);
            CHECK(harness.describerState.describedMeta->frameCount == 3);
            CHECK(harness.describerState.describedMeta->hasXmp);
            const auto &info = (*opened)->imageInfo();
            CHECK(info.pageCount == 3);
            CHECK(info.animated);
            CHECK(info.formatName == "Format name");
            CHECK(info.compression == "Codec name");
            CHECK(info.comments == "described by the plugin");
        }

        TEST_CASE("every session of one CodecPlugin borrows the plugin's sRGB output tables")
        {
            // The composition root builds one SrgbOutputTables per plugin instance and injects it
            // by reference; CodecPlugin hands that same instance to every session it opens, HDR
            // or not, and a second plugin over another instance hands out that one. The sessions
            // are the concrete FileSession the plugin constructs, hence the cast.
            auto hdrMeta = test::meta(3, 2, false, 10);
            hdrMeta.cicp = Cicp{12, 16, 12, true};
            Harness hdr(hdrMeta);
            Harness sdr;
            const std::vector wholeFile{std::byte{3}};

            auto first = hdr.plugin.open(pvd::OpenRequest{"a.bin", 0, wholeFile});
            auto second = hdr.plugin.open(pvd::OpenRequest{"b.bin", 0, wholeFile});
            auto third = sdr.plugin.open(pvd::OpenRequest{"c.bin", 0, wholeFile});
            REQUIRE(first.has_value());
            REQUIRE(second.has_value());
            REQUIRE(third.has_value());

            const auto &firstSession = dynamic_cast<const FileSession &>(**first);
            const auto &secondSession = dynamic_cast<const FileSession &>(**second);
            const auto &thirdSession = dynamic_cast<const FileSession &>(**third);
            CHECK(&firstSession.outputTables() == hdr.outputTables.get());
            CHECK(&secondSession.outputTables() == hdr.outputTables.get());
            CHECK(&thirdSession.outputTables() == sdr.outputTables.get());
            CHECK(&firstSession.outputTables() != &thirdSession.outputTables());

            // Opening sessions built nothing of its own: the plugin's tables are the ones it was
            // given, and they outlive every session by the harness's declaration order.
            const auto decoded = (*first)->decodePage(0, pvd::Progress{});
            REQUIRE(decoded.has_value());
            CHECK(decoded->bitsPerPixel == 64);
            CHECK((*first)->freePage(decoded->pixels));
        }

        TEST_CASE("CodecPlugin file mode parses opened file bytes and retains their "
                  "owner")
        {
            Harness harness;
            harness.fileState.fileBytes = {std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}};
            const std::vector head{std::byte{1}, std::byte{2}};

            auto opened = harness.plugin.open(pvd::OpenRequest{"C:/images/photo.bin", 4, head});

            REQUIRE(opened.has_value());
            CHECK(harness.fileState.openCalls == 1);
            CHECK(harness.fileState.openedPath == "C:/images/photo.bin");
            CHECK(harness.factoryState.lookedAt == head);
            CHECK(harness.factoryState.createdFrom == harness.fileState.fileBytes);
            CHECK(harness.factoryState.createdFrom != head);
            CHECK(harness.fileState.dataDestructions == 0);
            opened->reset();
            CHECK(harness.fileState.dataDestructions == 1);
        }

        TEST_CASE("CodecPlugin passes through file source errors without creating a "
                  "decoder")
        {
            Harness harness;
            harness.fileState.openError = test::error(ErrorCode::FileOpenFailed, "missing file");
            const std::vector head{std::byte{1}};

            const auto opened = harness.plugin.open(pvd::OpenRequest{"missing.bin", 10, head});

            REQUIRE_FALSE(opened.has_value());
            CHECK(opened.error().code == ErrorCode::FileOpenFailed);
            CHECK(opened.error().detail == "missing file");
            CHECK(harness.factoryState.createCalls == 0);
            CHECK(harness.describerState.describeCalls == 0);
        }

        TEST_CASE("CodecPlugin passes through factory errors and releases opened file "
                  "data")
        {
            Harness harness;
            harness.fileState.fileBytes = {std::byte{1}, std::byte{2}};
            harness.factoryState.createError = test::error(ErrorCode::ParseFailed, "bad container");
            const std::vector head{std::byte{1}};

            const auto opened = harness.plugin.open(pvd::OpenRequest{"bad.bin", 2, head});

            REQUIRE_FALSE(opened.has_value());
            CHECK(opened.error().code == ErrorCode::ParseFailed);
            CHECK(opened.error().detail == "bad container");
            CHECK(harness.fileState.dataDestructions == 1);
            CHECK(harness.decoderState.destructions == 0);
            CHECK(harness.describerState.describeCalls == 0);
        }

        TEST_CASE("canonical source, factory and describer interfaces destroy "
                  "polymorphically")
        {
            test::FileSourceState fileState;
            test::FactoryState factoryState;
            test::DecoderState decoderState;
            test::DescriberState describerState;
            std::unique_ptr<IFileSource> source = std::make_unique<test::FakeFileSource>(fileState);
            std::unique_ptr<IDecoderFactory> factory =
                std::make_unique<test::FakeDecoderFactory>(factoryState, decoderState, test::meta());
            std::unique_ptr<IImageDescriber> describer = std::make_unique<test::FakeImageDescriber>(describerState);

            source.reset();
            factory.reset();
            describer.reset();
            CHECK(source == nullptr);
            CHECK(factory == nullptr);
            CHECK(describer == nullptr);
        }

    } // namespace
} // namespace pvdkit::core
