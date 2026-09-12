#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "Fakes.hpp"
#include "core/AvifPlugin.hpp"

namespace avifpvd::core {
namespace {

pvd::PluginInfo pluginInfo() {
  return pvd::PluginInfo{10, "AVIF", "1.0.0", "static decoder"};
}

TEST_CASE("AvifPlugin returns its stored plugin information") {
  test::FileSourceState fileState;
  test::FactoryState factoryState;
  test::DecoderState decoderState;
  test::FakeFileSource source(fileState);
  test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
  AvifPlugin plugin(source, factory, test::options(), pluginInfo());

  CHECK(plugin.info().priority == 10);
  CHECK(plugin.info().name == "AVIF");
  CHECK(plugin.info().version == "1.0.0");
  CHECK(plugin.info().comments == "static decoder");
}

TEST_CASE("AvifPlugin rejects a failed signature before opening or parsing") {
  test::FileSourceState fileState;
  test::FactoryState factoryState;
  factoryState.looksLikeAvif = false;
  test::DecoderState decoderState;
  test::FakeFileSource source(fileState);
  test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
  AvifPlugin plugin(source, factory, test::options(), pluginInfo());
  const std::vector head{std::byte{1}, std::byte{2}};

  const auto opened = plugin.open(pvd::OpenRequest{"ignored.avif", 100, head});

  REQUIRE_FALSE(opened.has_value());
  CHECK(opened.error().code == ErrorCode::NotAvif);
  CHECK(factoryState.lookedAt == head);
  CHECK(factoryState.createCalls == 0);
  CHECK(fileState.openCalls == 0);
}

TEST_CASE("AvifPlugin memory mode parses the whole head without using the file "
          "source") {
  test::FileSourceState fileState;
  test::FactoryState factoryState;
  test::DecoderState decoderState;
  auto imageMeta = test::meta(7, 5, true, 10, 3, true);
  imageMeta.hasIcc = true;
  test::FakeFileSource source(fileState);
  test::FakeDecoderFactory factory(factoryState, decoderState, imageMeta);
  const auto decoderOptions = test::options(77);
  AvifPlugin plugin(source, factory, decoderOptions, pluginInfo());
  const std::vector wholeFile{std::byte{3}, std::byte{4}, std::byte{5}};

  auto opened =
      plugin.open(pvd::OpenRequest{"archive-entry.avif", 0, wholeFile});

  REQUIRE(opened.has_value());
  CHECK(fileState.openCalls == 0);
  CHECK(factoryState.createCalls == 1);
  CHECK(factoryState.createdFrom == wholeFile);
  CHECK(factoryState.receivedOptions.maxThreads == 2);
  CHECK(factoryState.receivedOptions.maxPixels == 77);
  CHECK((*opened)->imageInfo().pageCount == 3);
  CHECK((*opened)->imageInfo().animated);
  CHECK((*opened)->imageInfo().formatName == "AVIF");
  CHECK((*opened)->imageInfo().compression == "AV1");
  CHECK((*opened)->imageInfo().comments ==
        "10-bit YUV 4:2:0 (limited range), CICP 1/13/6 (sRGB), straight alpha, "
        "3 frames, ICC");
}

TEST_CASE(
    "AvifPlugin file mode parses opened file bytes and retains their owner") {
  test::FileSourceState fileState;
  fileState.fileBytes = {std::byte{9}, std::byte{8}, std::byte{7},
                         std::byte{6}};
  test::FactoryState factoryState;
  test::DecoderState decoderState;
  test::FakeFileSource source(fileState);
  test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
  AvifPlugin plugin(source, factory, test::options(), pluginInfo());
  const std::vector head{std::byte{1}, std::byte{2}};

  auto opened = plugin.open(pvd::OpenRequest{"C:/images/photo.avif", 4, head});

  REQUIRE(opened.has_value());
  CHECK(fileState.openCalls == 1);
  CHECK(fileState.openedPath == "C:/images/photo.avif");
  CHECK(factoryState.lookedAt == head);
  CHECK(factoryState.createdFrom == fileState.fileBytes);
  CHECK(factoryState.createdFrom != head);
  CHECK(fileState.dataDestructions == 0);
  opened->reset();
  CHECK(fileState.dataDestructions == 1);
}

TEST_CASE(
    "AvifPlugin passes through file source errors without creating a decoder") {
  test::FileSourceState fileState;
  fileState.openError = test::error(ErrorCode::FileOpenFailed, "missing file");
  test::FactoryState factoryState;
  test::DecoderState decoderState;
  test::FakeFileSource source(fileState);
  test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
  AvifPlugin plugin(source, factory, test::options(), pluginInfo());
  const std::vector head{std::byte{1}};

  const auto opened = plugin.open(pvd::OpenRequest{"missing.avif", 10, head});

  REQUIRE_FALSE(opened.has_value());
  CHECK(opened.error().code == ErrorCode::FileOpenFailed);
  CHECK(opened.error().detail == "missing file");
  CHECK(factoryState.createCalls == 0);
}

TEST_CASE(
    "AvifPlugin passes through factory errors and releases opened file data") {
  test::FileSourceState fileState;
  fileState.fileBytes = {std::byte{1}, std::byte{2}};
  test::FactoryState factoryState;
  factoryState.createError =
      test::error(ErrorCode::ParseFailed, "bad container");
  test::DecoderState decoderState;
  test::FakeFileSource source(fileState);
  test::FakeDecoderFactory factory(factoryState, decoderState, test::meta());
  AvifPlugin plugin(source, factory, test::options(), pluginInfo());
  const std::vector head{std::byte{1}};

  const auto opened = plugin.open(pvd::OpenRequest{"bad.avif", 2, head});

  REQUIRE_FALSE(opened.has_value());
  CHECK(opened.error().code == ErrorCode::ParseFailed);
  CHECK(opened.error().detail == "bad container");
  CHECK(fileState.dataDestructions == 1);
  CHECK(decoderState.destructions == 0);
}

TEST_CASE("canonical source and factory interfaces destroy polymorphically") {
  test::FileSourceState fileState;
  test::FactoryState factoryState;
  test::DecoderState decoderState;
  std::unique_ptr<IFileSource> source =
      std::make_unique<test::FakeFileSource>(fileState);
  std::unique_ptr<IDecoderFactory> factory =
      std::make_unique<test::FakeDecoderFactory>(factoryState, decoderState,
                                                 test::meta());

  source.reset();
  factory.reset();
  CHECK(source == nullptr);
  CHECK(factory == nullptr);
}

} // namespace
} // namespace avifpvd::core
