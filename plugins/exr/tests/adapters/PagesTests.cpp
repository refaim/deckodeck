#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "core/Describe.hpp"

#include "adapters/exr/Decoder.hpp"

namespace
{
    std::vector<std::byte> pageFixture(const std::string_view name)
    {
        const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
        std::ifstream input{path, std::ios::binary};
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(input.good());
        return bytes;
    }
} // namespace

TEST_CASE("EXR page catalogue exposes multipart views, multiview channels and complete colour layers")
{
    for (const auto name : std::array<std::string_view, 4>{"multipart_views.exr", "multiview_left_first.exr",
                                                           "multiview_right_first.exr", "layer_rgb.exr"}) {
        CAPTURE(name);
        const auto bytes = pageFixture(name);
        pvdkit::exr::DecoderFactory factory;
        auto opened =
            factory.create(bytes, pvdkit::core::DecoderOptions{4, false, std::uint64_t{16'384} * 16'384, 32'768, true});
        REQUIRE(opened.has_value());
        auto &decoder = **opened;
        CHECK(decoder.meta().frameCount == 2);
        CHECK_FALSE(decoder.meta().animated);
        const auto timing = decoder.frameTiming(1);
        REQUIRE(timing.has_value());
        CHECK(timing->durationMs == 0);
        const auto pitch = decoder.meta().width * 8;
        std::vector<std::byte> first(static_cast<std::size_t>(pitch) * decoder.meta().height);
        std::vector<std::byte> second(first.size());
        REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, first, pitch));
        REQUIRE(decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Bgra64, second, pitch));
        CHECK(first != second);
        std::vector<std::byte> revisited(first.size());
        REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, revisited, pitch));
        CHECK(first == revisited);
        CHECK_FALSE(decoder.frameTiming(2));
        CHECK_FALSE(decoder.decodeFrame(2, pvdkit::pvd::PixelFormat::Bgra64, second, pitch));
    }
}

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticities.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/ImfStringVectorAttribute.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

namespace
{
    struct PagesFile
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("deckodeck-pages-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".exr");
        ~PagesFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };

    struct TestChannel
    {
        std::string name;
        float value;
        int sampling = 1;
        Imf::PixelType type = Imf::FLOAT;
    };

    std::vector<std::byte> channelFile(const std::vector<TestChannel> &channels,
                                       const std::vector<std::string> &views = {}, const std::string_view partView = {})
    {
        PagesFile file;
        const auto utf8 = file.path.u8string();
        const std::string path{utf8.begin(), utf8.end()};
        Imf::Header header{4, 2};
        header.compression() = Imf::NO_COMPRESSION;
        if (!views.empty()) {
            header.insert("multiView", Imf::StringVectorAttribute{views});
        }
        if (!partView.empty()) {
            header.insert("view", Imf::StringAttribute{std::string{partView}});
        }
        std::vector<std::array<float, 8>> samples(channels.size());
        Imf::FrameBuffer frame;
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const auto &channel = channels[index];
            header.channels().insert(channel.name, Imf::Channel{channel.type, channel.sampling, channel.sampling});
            samples[index].fill(channel.value);
            frame.insert(channel.name,
                         Imf::Slice{Imf::FLOAT, reinterpret_cast<char *>(samples[index].data()), sizeof(float),
                                    4 * sizeof(float), channel.sampling, channel.sampling});
        }
        {
            Imf::OutputFile output{path.c_str(), header, 1};
            output.setFrameBuffer(frame);
            output.writePixels(2);
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(file.path)));
        std::ifstream input{file.path, std::ios::binary};
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(input.good());
        return bytes;
    }

    std::vector<std::byte> layeredViews()
    {
        return channelFile({{"beauty.left.R", 0.5F},
                            {"beauty.left.G", 0.5F},
                            {"beauty.left.B", 0.5F},
                            {"beauty.right.R", 4.0F},
                            {"beauty.right.G", 4.0F},
                            {"beauty.right.B", 4.0F},
                            {"beauty.right.A", 0.25F},
                            {"utility.left.Z", 4.0F}},
                           {"left", "right"});
    }
} // namespace
TEST_CASE("EXR page catalogue keeps nested view layers separate and alpha independent")
{
    const auto bytes = layeredViews();
    pvdkit::exr::DecoderFactory factory;
    auto opened = factory.create(bytes, {4, false, 1024, 1024, true, true});
    REQUIRE(opened);
    auto &decoder = **opened;
    REQUIRE(decoder.meta().frameCount == 2);
    const auto first = decoder.pageMeta(0);
    const auto second = decoder.pageMeta(1);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->forceHighPrecision);
    CHECK_FALSE(first->image.hasAlpha);
    CHECK(second->image.hasAlpha);
    CHECK(second->image.depth == 32);
    CHECK_FALSE(second->image.masteringPeakNits);
    CHECK(second->image.sourceDetail == "2 pages, multiview");
    CHECK_FALSE(decoder.pageMeta(2));
    std::vector<std::byte> left(4 * 2 * 6);
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Rgb48, left, 4 * 6));
    const auto leftPeak = decoder.decodedColour().masteringPeakNits;
    REQUIRE(leftPeak);
    std::vector<std::byte> right(4 * 2 * 8);
    REQUIRE(decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Rgba64, right, 4 * 8));
    CHECK(decoder.decodedColour().masteringPeakNits > leftPeak);
    CHECK(std::to_integer<unsigned>(right[6]) == 0);
    CHECK(std::to_integer<unsigned>(right[7]) == 64);
    CHECK(decoder.pageMeta(1)->image.masteringPeakNits == decoder.decodedColour().masteringPeakNits);
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Rgb48, left, 4 * 6));
    CHECK(decoder.decodedColour().masteringPeakNits == leftPeak);
    CHECK_FALSE(decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Rgb48, left, 4 * 6));
    CHECK(decoder.decodedColour().masteringPeakNits == leftPeak);
}

TEST_CASE("EXR lazy secondary part failure leaves decoded colour and host pixels intact and can retry")
{
    auto bytes = pageFixture("multipart_views.exr");
    const pvdkit::core::DecoderOptions options{4, false, 1024, 1024, true};
    pvdkit::exr::Stream stream{bytes};
    const auto context = pvdkit::exr::openContext(stream, options);
    REQUIRE(context);
    exr_chunk_info_t chunk{};
    REQUIRE(exr_read_scanline_chunk_info(context->get(), 0, 0, &chunk) == EXR_ERR_SUCCESS);
    REQUIRE(chunk.packed_size < chunk.unpacked_size);
    const auto original = bytes;
    std::ranges::fill(std::span{bytes}.subspan(static_cast<std::size_t>(chunk.data_offset),
                                               static_cast<std::size_t>(chunk.packed_size)),
                      std::byte{0xFF});
    pvdkit::exr::DecoderFactory factory;
    auto opened = factory.create(bytes, options);
    REQUIRE(opened);
    auto &decoder = **opened;
    const auto peak = decoder.decodedColour().masteringPeakNits;
    std::vector<std::byte> first(8 * 8 * 8);
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, first, 8 * 8));
    const auto retained = first;
    std::vector<std::byte> failed(first.size(), std::byte{0xAC});
    const auto failure = decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Bgra64, failed, 8 * 8);
    REQUIRE_FALSE(failure);
    CHECK(failure.error().code == pvdkit::core::ErrorCode::DecodeFailed);
    CHECK(decoder.decodedColour().masteringPeakNits == peak);
    CHECK(std::ranges::all_of(failed, [](auto byte) { return byte == std::byte{0xAC}; }));
    CHECK(first == retained);
    // Restore in place: the decoder borrows this stable allocation, just like FileSession's mapping.
    std::ranges::copy(original, bytes.begin());
    REQUIRE(decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Bgra64, failed, 8 * 8));
    CHECK(failed != first);
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, failed, 8 * 8));
    CHECK(failed == first);
    CHECK(first == retained);
}

TEST_CASE("EXR page catalogue includes luminance and chroma layers without auxiliary pseudo pages")
{
    const auto bytes = channelFile({{"R", 0.5F},
                                    {"G", 0.5F},
                                    {"B", 0.5F},
                                    {"luma.Y", 0.25F},
                                    {"yc.Y", 0.75F},
                                    {"yc.RY", 0.0F, 2},
                                    {"yc.BY", 0.0F, 2},
                                    {"utility.Z", 7.0F},
                                    {"mask.A", 0.2F}});
    pvdkit::exr::DecoderFactory factory;
    auto opened = factory.create(bytes, {1, false, 1024, 1024, true});
    REQUIRE(opened);
    auto &decoder = **opened;
    REQUIRE(decoder.meta().frameCount == 3);
    CHECK(decoder.pageMeta(1)->image.chroma == pvdkit::core::ChromaFormat::Yuv400);
    CHECK(decoder.pageMeta(2)->image.chroma == pvdkit::core::ChromaFormat::Yuv420);
    std::vector<std::byte> rgb(4 * 2 * 8), luma(rgb.size()), yc(rgb.size());
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, rgb, 4 * 8));
    REQUIRE(decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Bgra64, luma, 4 * 8));
    REQUIRE(decoder.decodeFrame(2, pvdkit::pvd::PixelFormat::Bgra64, yc, 4 * 8));
    CHECK(rgb != luma);
    CHECK(rgb != yc);
    const auto bare = channelFile({{"Y", 0.75F}, {"RY", 0.0F, 2}, {"BY", 0.0F, 2}});
    auto reference = factory.create(bare, {1, false, 1024, 1024, true});
    REQUIRE(reference);
    std::vector<std::byte> expected(yc.size());
    REQUIRE((*reference)->decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, expected, 4 * 8));
    CHECK(yc == expected);
}

TEST_CASE("EXR implicit first view alpha stays in its view and lone grey channels retain one fallback page")
{
    const auto bytes = channelFile(
        {{"R", 0.1F}, {"G", 0.1F}, {"B", 0.1F}, {"A", 0.5F}, {"right.R", 0.3F}, {"right.G", 0.3F}, {"right.B", 0.3F}},
        {"left", "right"});
    pvdkit::exr::DecoderFactory factory;
    auto opened = factory.create(bytes, {1, false, 1024, 1024, true, true});
    REQUIRE(opened);
    REQUIRE((*opened)->meta().frameCount == 2);
    CHECK((*opened)->pageMeta(0)->image.hasAlpha);
    CHECK_FALSE((*opened)->pageMeta(1)->image.hasAlpha);
    std::vector<std::byte> right(4 * 2 * 6);
    REQUIRE((*opened)->decodeFrame(1, pvdkit::pvd::PixelFormat::Rgb48, right, 4 * 6));
    const auto grey = channelFile({{"Z", 0.25F}, {"utility.mask", 0.75F}});
    auto single = factory.create(grey, {1, false, 1024, 1024, true});
    REQUIRE(single);
    CHECK((*single)->meta().frameCount == 1);
    std::vector<std::byte> pixels(4 * 2 * 8);
    REQUIRE((*single)->decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, pixels, 4 * 8));
    CHECK(std::equal(pixels.begin(), pixels.begin() + 2, pixels.begin() + 2));
    CHECK(std::equal(pixels.begin(), pixels.begin() + 2, pixels.begin() + 4));
}

TEST_CASE("EXR part-wide view does not strip a real layer or combine its channels with bare channels")
{
    pvdkit::exr::DecoderFactory factory;
    const auto bytes = channelFile({{"R", 0.25F},
                                    {"G", 0.25F},
                                    {"B", 0.25F},
                                    {"left.R", 1.0F},
                                    {"left.G", 1.0F},
                                    {"left.B", 1.0F},
                                    {"left.A", 0.5F}},
                                   {}, "left");
    auto opened = factory.create(bytes, {1, false, 1024, 1024, true});
    REQUIRE(opened);
    REQUIRE((*opened)->meta().frameCount == 2);
    CHECK_FALSE((*opened)->pageMeta(0)->image.hasAlpha);
    CHECK((*opened)->pageMeta(1)->image.hasAlpha);
    CHECK((*opened)->pageMeta(1)->image.sourceDetail == "2 pages, multiview");
    std::vector<std::byte> bare(4 * 2 * 8), layer(bare.size());
    REQUIRE((*opened)->decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, bare, 4 * 8));
    REQUIRE((*opened)->decodeFrame(1, pvdkit::pvd::PixelFormat::Bgra64, layer, 4 * 8));
    CHECK(std::to_integer<unsigned>(bare[6]) == 255);
    CHECK(std::to_integer<unsigned>(bare[7]) == 255);
    CHECK(std::to_integer<unsigned>(layer[6]) == 0);
    CHECK(std::to_integer<unsigned>(layer[7]) == 128);
    CHECK(bare != layer);
    // 200 nit (1.0 scene unit / 0.5 alpha * 100 nit) is PQ code 37953.
    const auto code =
        static_cast<unsigned>(std::to_integer<unsigned>(layer[0]) | (std::to_integer<unsigned>(layer[1]) << 8U));
    CHECK(code == 37953);
    const auto mixed = channelFile({{"R", 0.25F}, {"left.G", 0.5F}, {"left.B", 0.75F}}, {}, "left");
    auto grey = factory.create(mixed, {1, false, 1024, 1024, true});
    REQUIRE(grey);
    CHECK((*grey)->meta().frameCount == 1);
    CHECK((*grey)->meta().chroma == pvdkit::core::ChromaFormat::Yuv400);
}

TEST_CASE("EXR decoder with borrowed factory tables cannot be copied or moved")
{
    CHECK_FALSE(std::is_copy_constructible_v<pvdkit::exr::Decoder>);
    CHECK_FALSE(std::is_copy_assignable_v<pvdkit::exr::Decoder>);
    CHECK_FALSE(std::is_move_constructible_v<pvdkit::exr::Decoder>);
    CHECK_FALSE(std::is_move_assignable_v<pvdkit::exr::Decoder>);
}

TEST_CASE("EXR preferred normalized group remains exactly one page when alias alpha changes its selection")
{
    pvdkit::exr::DecoderFactory factory;
    const auto bytes = channelFile({{"R", 0.25F}, {"G", 0.25F}, {"B", 0.25F}, {"left.A", 0.5F}}, {"left", "right"});
    auto opened = factory.create(bytes, {1, false, 1024, 1024, true});
    REQUIRE(opened);
    CHECK((*opened)->meta().frameCount == 1);
    CHECK_FALSE((*opened)->meta().hasAlpha);
    CHECK((*opened)->meta().chroma == pvdkit::core::ChromaFormat::Yuv444);
    CHECK_FALSE((*opened)->pageMeta(1));
    std::vector<std::byte> pixels(4 * 2 * 8);
    REQUIRE((*opened)->decodeFrame(0, pvdkit::pvd::PixelFormat::Bgra64, pixels, 4 * 8));
    CHECK(std::to_integer<unsigned>(pixels[6]) == 255);
    CHECK(std::to_integer<unsigned>(pixels[7]) == 255);
}

TEST_CASE("EXR preferred single-channel layer is not appended again as normalized luminance")
{
    const auto bytes = channelFile({{"layer.A", 0.25F}, {"layer.Y", 0.75F}});
    pvdkit::exr::DecoderFactory factory;
    auto opened = factory.create(bytes, {1, false, 1024, 1024, true});
    REQUIRE(opened);
    CHECK((*opened)->meta().frameCount == 1);
    CHECK_FALSE((*opened)->meta().hasAlpha);
    CHECK((*opened)->meta().chroma == pvdkit::core::ChromaFormat::Yuv400);
    CHECK((*opened)->meta().sourceDetail == "float grey, linear Rec.709");
    CHECK_FALSE((*opened)->pageMeta(1));
}

TEST_CASE("EXR multipart pages retain independent precision colour exposure and measured peak")
{
    PagesFile file;
    const auto utf8 = file.path.u8string();
    const std::string path{utf8.begin(), utf8.end()};
    std::array headers{Imf::Header{4, 2}, Imf::Header{4, 2}};
    const std::array names{"R", "G", "B", "A"};
    Imf::FrameBuffer floatFrame, halfFrame;
    std::array<std::array<float, 8>, 3> floatPlanes{};
    std::array<std::array<Imath::half, 8>, 4> halfPlanes{};
    for (std::size_t part = 0; part < headers.size(); ++part) {
        headers[part].setName(part == 0 ? "floatRight" : "halfLeft");
        headers[part].setType(Imf::SCANLINEIMAGE);
        headers[part].compression() = Imf::NO_COMPRESSION;
        headers[part].insert("view", Imf::StringAttribute{part == 0 ? "right" : "left"});
        // Explicit chromaticities and displayWindow are shared multipart attributes.
        // colorInteropID and whiteLuminance may vary independently between parts.
        headers[part].insert("colorInteropID", Imf::StringAttribute{part == 0 ? "lin_rec2020" : "lin_ap1"});
        headers[part].insert("whiteLuminance", Imf::FloatAttribute{part == 0 ? 250.0F : 1000.0F});
    }
    for (std::size_t channel = 0; channel < names.size(); ++channel) {
        halfPlanes[channel].fill(Imath::half{channel == 3 ? 1.0F : 10.0F});
        headers[1].channels().insert(names[channel], Imf::Channel{Imf::HALF});
        halfFrame.insert(names[channel], Imf::Slice{Imf::HALF, reinterpret_cast<char *>(halfPlanes[channel].data()),
                                                    sizeof(Imath::half), 4 * sizeof(Imath::half)});
        if (channel < 3) {
            floatPlanes[channel].fill(1.0F);
            headers[0].channels().insert(names[channel], Imf::Channel{Imf::FLOAT});
            floatFrame.insert(names[channel],
                              Imf::Slice{Imf::FLOAT, reinterpret_cast<char *>(floatPlanes[channel].data()),
                                         sizeof(float), 4 * sizeof(float)});
        }
    }
    {
        Imf::MultiPartOutputFile output{path.c_str(), headers.data(), 2, false, 1};
        Imf::OutputPart right{output, 0};
        right.setFrameBuffer(floatFrame);
        right.writePixels(2);
        Imf::OutputPart left{output, 1};
        left.setFrameBuffer(halfFrame);
        left.writePixels(2);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(file.path)));
    std::ifstream input{file.path, std::ios::binary};
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.good());
    pvdkit::exr::DecoderFactory factory;
    auto opened = factory.create(bytes, {1, false, 1024, 1024, true, true});
    REQUIRE(opened);
    auto &decoder = **opened;
    REQUIRE(decoder.meta().frameCount == 2);
    const auto first = decoder.pageMeta(0);
    const auto second = decoder.pageMeta(1);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->image.depth == 16);
    CHECK(first->image.hasAlpha);
    CHECK(first->image.cicp.primaries == 2);
    CHECK(first->image.chromaticities.has_value());
    REQUIRE(first->image.masteringPeakNits);
    CHECK(*first->image.masteringPeakNits == 10000.0F);
    CHECK(second->image.depth == 32);
    CHECK_FALSE(second->image.hasAlpha);
    CHECK(second->image.cicp.primaries == 9);
    CHECK_FALSE(second->image.chromaticities);
    CHECK_FALSE(second->image.masteringPeakNits);
    std::vector<std::byte> rgba(4 * 2 * 8), rgb(4 * 2 * 6);
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Rgba64, rgba, 4 * 8));
    CHECK(std::ranges::all_of(rgba, [](auto byte) { return byte == std::byte{0xFF}; }));
    REQUIRE(decoder.decodeFrame(1, pvdkit::pvd::PixelFormat::Rgb48, rgb, 4 * 6));
    const auto colour = decoder.decodedColour();
    CHECK(colour.cicp.primaries == 9);
    CHECK_FALSE(colour.chromaticities);
    REQUIRE(colour.masteringPeakNits);
    CHECK(*colour.masteringPeakNits == doctest::Approx(250.0F).epsilon(0.001));
    CHECK(decoder.pageMeta(1)->image.masteringPeakNits == colour.masteringPeakNits);
    // Double-precision ST2084 at 250 nit rounds to 39489 (0x9A41).
    CHECK(std::to_integer<unsigned>(rgb[0]) == 65);
    CHECK(std::to_integer<unsigned>(rgb[1]) == 154);
    REQUIRE(decoder.decodeFrame(0, pvdkit::pvd::PixelFormat::Rgba64, rgba, 4 * 8));
    CHECK(decoder.decodedColour().cicp.primaries == 2);
    CHECK(decoder.decodedColour().chromaticities.has_value());
    CHECK(decoder.decodedColour().masteringPeakNits == first->image.masteringPeakNits);
}

TEST_CASE("EXR file-wide title stays truthful across multipart multiview and layer pages")
{
    struct ExpectedTitle
    {
        std::string_view fixture;
        std::string_view title;
    };
    const std::array cases{
        ExpectedTitle{"multipart_views.exr", "2 pages, multipart, multiview → sRGB"},
        ExpectedTitle{"multiview_left_first.exr", "2 pages, multiview → sRGB"},
        ExpectedTitle{"multiview_right_first.exr", "2 pages, multiview → sRGB"},
        ExpectedTitle{"layer_rgb.exr", "2 pages, layers → sRGB"},
    };
    pvdkit::exr::DecoderFactory factory;
    for (const auto &item : cases) {
        CAPTURE(item.fixture);
        const auto bytes = pageFixture(item.fixture);
        auto opened = factory.create(bytes, {1, false, 1024, 1024, true});
        REQUIRE(opened);
        auto &decoder = **opened;
        const pvdkit::exr::Describer describer;
        CHECK(describer.describe(decoder.meta()).comments == item.title);
        CHECK(describer.describe(decoder.meta()).compression.empty());
        std::vector<std::byte> pixels(static_cast<std::size_t>(decoder.meta().width) * decoder.meta().height * 8);
        for (std::uint32_t page = 0; page < 2; ++page) {
            REQUIRE(decoder.decodeFrame(page, pvdkit::pvd::PixelFormat::Bgra64, pixels, decoder.meta().width * 8));
            CHECK(describer.describe(decoder.meta()).comments == item.title);
            CHECK(describer.describe(decoder.pageMeta(page)->image).comments == item.title);
        }
    }
}

TEST_CASE("EXR multipart title reports multiview declarations from every part")
{
    PagesFile file;
    const auto utf8 = file.path.u8string();
    const std::string path{utf8.begin(), utf8.end()};
    std::array headers{Imf::Header{4, 2}, Imf::Header{4, 2}};
    headers[1].insert("multiView", Imf::StringVectorAttribute{{"left", "right"}});
    std::array<float, 8> samples{};
    samples.fill(0.25F);
    Imf::FrameBuffer frame;
    for (const auto name : {"R", "G", "B"}) {
        for (std::size_t part = 0; part < headers.size(); ++part) {
            headers[part].setName(std::to_string(part));
            headers[part].setType(Imf::SCANLINEIMAGE);
            headers[part].channels().insert(name, Imf::Channel{Imf::FLOAT});
        }
        frame.insert(
            name, Imf::Slice{Imf::FLOAT, reinterpret_cast<char *>(samples.data()), sizeof(float), 4 * sizeof(float)});
    }
    {
        Imf::MultiPartOutputFile output{path.c_str(), headers.data(), 2, false, 1};
        for (int part = 0; part < 2; ++part) {
            Imf::OutputPart writer{output, part};
            writer.setFrameBuffer(frame);
            writer.writePixels(2);
        }
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(file.path)));
    std::ifstream input{file.path, std::ios::binary};
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.good());
    pvdkit::exr::DecoderFactory factory;
    const auto opened = factory.create(bytes, {1, false, 1024, 1024, true});
    REQUIRE(opened);
    REQUIRE((*opened)->meta().frameCount == 2);
    CHECK((*opened)->meta().sourceDetail == "2 pages, multipart, multiview");
    CHECK((*opened)->pageMeta(1)->image.sourceDetail == "2 pages, multipart, multiview");
}
