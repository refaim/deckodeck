#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "adapters/resvg/Decoder.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Plugin.hpp"
#include "pvd/PluginFactory.hpp"

namespace pvdkit::svg::tests
{
    namespace
    {

        namespace fs = std::filesystem;
        using core::ErrorCode;
        using core::ImageMeta;

        struct Sample
        {
            std::uint32_t x, y;
            std::array<std::uint8_t, 4> rgba;
            std::uint8_t tolerance;
        };

        struct FixtureCase
        {
            std::string_view name;
            std::uint32_t width, height;
            bool hasNonOpaquePixels;
            std::vector<Sample> samples;
        };

        const std::vector<FixtureCase> &acceptedFixtures()
        {
            static const std::vector<FixtureCase> fixtures{
                {"alpha-composite.svg", 1, 1, true, {{0, 0, {170, 0, 85, 191}, 2}}},
                {"alpha-ramp.svg",
                 3,
                 1,
                 true,
                 {{0, 0, {0, 0, 0, 0}, 0}, {1, 0, {255, 0, 0, 128}, 2}, {2, 0, {0, 255, 0, 255}, 0}}},
                {"basic-2x2-colors.svg",
                 2,
                 2,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {1, 0, {0, 255, 0, 255}, 0},
                  {0, 1, {0, 0, 255, 255}, 0},
                  {1, 1, {255, 255, 0, 255}, 0}}},
                {"clip.svg",
                 4,
                 2,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {1, 1, {255, 0, 0, 255}, 0},
                  {2, 0, {0, 0, 255, 255}, 0},
                  {3, 1, {0, 0, 255, 255}, 0}}},
                {"filter-color-matrix.svg",
                 2,
                 2,
                 false,
                 {{0, 0, {100, 50, 25, 255}, 0},
                  {1, 0, {100, 50, 25, 255}, 0},
                  {0, 1, {100, 50, 25, 255}, 0},
                  {1, 1, {100, 50, 25, 255}, 0}}},
                {"gradient-linear.svg",
                 4,
                 1,
                 false,
                 {{0, 0, {32, 32, 32, 255}, 3},
                  {1, 0, {96, 96, 96, 255}, 3},
                  {2, 0, {159, 159, 159, 255}, 3},
                  {3, 0, {223, 223, 223, 255}, 3}}},
                {"image-data-uri.svg",
                 2,
                 2,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {1, 0, {0, 255, 0, 255}, 0},
                  {0, 1, {0, 0, 255, 255}, 0},
                  {1, 1, {255, 255, 0, 255}, 0}}},
                {"image-relative.svg",
                 2,
                 2,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {1, 0, {0, 255, 0, 255}, 0},
                  {0, 1, {0, 0, 255, 255}, 0},
                  {1, 1, {255, 255, 0, 255}, 0}}},
                {"mask.svg",
                 4,
                 2,
                 true,
                 {{0, 0, {0, 0, 0, 0}, 0},
                  {1, 1, {0, 0, 0, 0}, 0},
                  {2, 0, {0, 255, 0, 255}, 0},
                  {3, 1, {0, 255, 0, 255}, 0}}},
                {"text-cyrillic.svg",
                 160,
                 48,
                 false,
                 {{0, 0, {255, 255, 255, 255}, 0}, {159, 47, {255, 255, 255, 255}, 0}}},
                {"transform-rotate-90.svg",
                 4,
                 4,
                 true,
                 {{3, 1, {0, 255, 0, 255}, 0},
                  {3, 2, {0, 255, 0, 255}, 0},
                  {1, 0, {0, 0, 0, 0}, 0},
                  {2, 2, {0, 0, 0, 0}, 0}}},
                {"transform-translate-scale.svg",
                 4,
                 4,
                 true,
                 {{0, 0, {0, 0, 0, 0}, 0},
                  {1, 1, {255, 0, 0, 255}, 0},
                  {2, 2, {255, 0, 0, 255}, 0},
                  {3, 3, {0, 0, 0, 0}, 0}}},
                {"units-in.svg",
                 96,
                 48,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {47, 47, {255, 0, 0, 255}, 0},
                  {48, 0, {0, 0, 255, 255}, 0},
                  {95, 47, {0, 0, 255, 255}, 0}}},
                {"valid.svgz",
                 2,
                 2,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {1, 0, {0, 255, 0, 255}, 0},
                  {0, 1, {0, 0, 255, 255}, 0},
                  {1, 1, {255, 255, 0, 255}, 0}}},
                {"viewbox-only.svg",
                 6,
                 3,
                 false,
                 {{0, 0, {255, 0, 0, 255}, 0},
                  {2, 2, {255, 0, 0, 255}, 0},
                  {3, 0, {0, 255, 0, 255}, 0},
                  {5, 2, {0, 255, 0, 255}, 0}}},
                {"viewbox-scale.svg",
                 8,
                 4,
                 false,
                 {{1, 1, {255, 0, 0, 255}, 0},
                  {5, 1, {0, 255, 0, 255}, 0},
                  {1, 3, {0, 0, 255, 255}, 0},
                  {5, 3, {255, 255, 0, 255}, 0}}},
                {"xml-declaration-comments.svg",
                 3,
                 1,
                 false,
                 {{0, 0, {18, 52, 86, 255}, 0}, {1, 0, {18, 52, 86, 255}, 0}, {2, 0, {18, 52, 86, 255}, 0}}},
            };
            return fixtures;
        }

        std::vector<std::byte> readFixture(const std::string_view name)
        {
            const auto path = fs::path{PVDKIT_FIXTURE_DIR} / name;
            std::vector<std::byte> bytes(static_cast<std::size_t>(fs::file_size(path)));
            std::ifstream stream{path, std::ios::binary};
            REQUIRE(stream.good());
            stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
            return bytes;
        }

        std::wstring fixturePath(const std::string_view name)
        {
            return (fs::path{PVDKIT_FIXTURE_DIR} / name).wstring();
        }

        core::DecoderOptions testOptions(const std::wstring_view sourcePath = {})
        {
            core::DecoderOptions options{1, false, 1'000'000, 16'384, false, true};
            if (!sourcePath.empty()) {
                options.sourcePath = sourcePath;
            }
            return options;
        }

        std::unique_ptr<core::IDecoder> requireDecoder(DecoderFactory &factory, const std::span<const std::byte> bytes,
                                                       const core::DecoderOptions &options)
        {
            auto created = factory.create(bytes, options);
            const std::string detail = created ? std::string{} : created.error().detail;
            CAPTURE(detail);
            REQUIRE(created.has_value());
            return std::move(*created);
        }

        std::vector<std::byte> render(core::IDecoder &decoder, const pvd::PixelFormat format)
        {
            const auto &meta = decoder.meta();
            const auto bytesPerPixel = pvd::pixelLayout(format).bytesPerPixel;
            const auto pitch = static_cast<std::uint32_t>(meta.width * bytesPerPixel);
            std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * meta.height);
            REQUIRE(decoder.decodeFrame(0, format, pixels, pitch).has_value());
            return pixels;
        }

        std::array<std::uint8_t, 4> pixel(const std::span<const std::byte> pixels, const std::uint32_t width,
                                          const std::uint32_t bytesPerPixel, const std::uint32_t x,
                                          const std::uint32_t y)
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * bytesPerPixel;
            std::array<std::uint8_t, 4> result{};
            result[3] = 255;
            for (std::uint32_t channel = 0; channel < bytesPerPixel; ++channel) {
                result[channel] = std::to_integer<std::uint8_t>(pixels[offset + channel]);
            }
            return result;
        }

        void checkSamples(const FixtureCase &fixture, const std::span<const std::byte> pixels)
        {
            for (const auto &sample : fixture.samples) {
                const auto actual = pixel(pixels, fixture.width, 4, sample.x, sample.y);
                CAPTURE(fixture.name);
                CAPTURE(sample.x);
                CAPTURE(sample.y);
                for (std::size_t channel = 0; channel < actual.size(); ++channel) {
                    CHECK(std::abs(static_cast<int>(actual[channel]) - static_cast<int>(sample.rgba[channel])) <=
                          sample.tolerance);
                }
            }
        }

        std::size_t darkPixelCount(const std::span<const std::byte> pixels, const std::uint32_t width,
                                   const std::uint32_t height)
        {
            std::size_t count = 0;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto value = pixel(pixels, width, 4, x, y);
                    if (value[0] < 128 && value[1] < 128 && value[2] < 128) {
                        ++count;
                    }
                }
            }
            return count;
        }

        std::vector<std::byte> textBytes(const std::string_view text)
        {
            std::vector<std::byte> bytes;
            bytes.reserve(text.size());
            for (const char value : text) {
                bytes.push_back(static_cast<std::byte>(value));
            }
            return bytes;
        }

        std::vector<std::byte> sizedSvg(const std::string_view width, const std::string_view height)
        {
            std::string text{"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""};
            text.append(width);
            text.append("\" height=\"");
            text.append(height);
            text.append("\"/>");
            return textBytes(text);
        }

        core::DecoderOptions unlimitedOptions()
        {
            core::DecoderOptions options{};
            options.maxDimension = 0;
            options.maxPixels = 0;
            return options;
        }

        class UnicodeFixtureCopy
        {
          public:
            UnicodeFixtureCopy()
                : directory_(fs::temp_directory_path() /
                             (L"pvdkit-svg-\u65e5\u672c\u8a9e-" +
                              std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()))),
                  svg_(directory_ / L"image-relative.svg")
            {
                REQUIRE(fs::create_directories(directory_ / L"assets"));
                REQUIRE(fs::copy_file(fs::path{PVDKIT_FIXTURE_DIR} / "image-relative.svg", svg_));
                REQUIRE(fs::copy_file(fs::path{PVDKIT_FIXTURE_DIR} / "assets/checker-2x2.png",
                                      directory_ / L"assets/checker-2x2.png"));
            }

            ~UnicodeFixtureCopy()
            {
                try {
                    std::error_code ignored;
                    fs::remove_all(directory_, ignored);
                } catch (...) {
                    return;
                }
            }

            UnicodeFixtureCopy(const UnicodeFixtureCopy &) = delete;
            UnicodeFixtureCopy &operator=(const UnicodeFixtureCopy &) = delete;

            [[nodiscard]] const fs::path &svg() const noexcept
            {
                return svg_;
            }

          private:
            fs::path directory_;
            fs::path svg_;
        };

        class TemporarySvgFile
        {
          public:
            TemporarySvgFile(const std::string_view label, const std::string_view contents)
                : directory_(fs::temp_directory_path() /
                             (L"pvdkit-svg-entry-" + std::wstring{label.begin(), label.end()} + L"-" +
                              std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()))),
                  path_(directory_ / L"entry.svg")
            {
                REQUIRE(fs::create_directory(directory_));
                std::ofstream stream{path_, std::ios::binary};
                REQUIRE(stream.good());
                stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                REQUIRE(stream.good());
            }

            ~TemporarySvgFile()
            {
                try {
                    std::error_code ignored;
                    fs::remove_all(directory_, ignored);
                } catch (...) {
                    return;
                }
            }

            TemporarySvgFile(const TemporarySvgFile &) = delete;
            TemporarySvgFile &operator=(const TemporarySvgFile &) = delete;

            [[nodiscard]] const fs::path &path() const noexcept
            {
                return path_;
            }

          private:
            fs::path directory_;
            fs::path path_;
        };

        std::string utf8Path(const fs::path &path)
        {
            const auto utf8 = path.u8string();
            return std::string{utf8.begin(), utf8.end()};
        }

    } // namespace

    TEST_CASE("SVG resvg accepts DOCTYPE prefixed roots and prologs longer than 4096 bytes")
    {
        SvgState state;
        DecoderFactory factory{state};
        std::string longProlog(5000, ' ');
        longProlog += "<!-- long prolog -->\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>";
        const std::array documents{
            std::string{"<?xml version=\"1.0\"?>\n"
                        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                        "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n"
                        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>"},
            std::string{"<s:svg xmlns:s=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>"},
            std::move(longProlog)};

        for (const auto &document : documents) {
            CAPTURE(document.size());
            const auto created = factory.create(textBytes(document), unlimitedOptions());
            const std::string detail = created ? std::string{} : created.error().detail;
            CAPTURE(detail);
            REQUIRE(created.has_value());
            CHECK((*created)->meta().width == 2);
            CHECK((*created)->meta().height == 2);
        }
    }

    TEST_CASE("SVG composed open accepts DOCTYPE prefixed roots and prologs longer than 4096 bytes")
    {
        const auto plugin = pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        CHECK(plugin->info().name == "SVG");
        CHECK(plugin->info().version == "1.0.0");
        std::string longProlog(5000, ' ');
        longProlog += "<!-- long prolog -->\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>";
        const std::array documents{
            std::string{"<?xml version=\"1.0\"?>\n"
                        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                        "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n"
                        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>"},
            std::string{"<s:svg xmlns:s=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>"},
            std::move(longProlog)};

        for (const auto &document : documents) {
            CAPTURE(document.size());
            auto bytes = textBytes(document);
            pvd::OpenRequest request{};
            request.head = bytes;
            const auto opened = plugin->open(request);
            const std::string detail = opened ? std::string{} : opened.error().detail;
            CAPTURE(detail);
            REQUIRE(opened.has_value());
            CHECK((*opened)->imageInfo().formatName == "SVG");
        }
    }

    TEST_CASE("SVG composed disk open reads the full source after a whitespace-only head")
    {
        std::string document(32, ' ');
        document.append(5000, ' ');
        document += "<!-- root follows the supplied head -->\n"
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>";
        const TemporarySvgFile file{"whitespace", document};
        const auto plugin = pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        const auto bytes = textBytes(document);
        const std::string name = utf8Path(file.path());
        pvd::OpenRequest request{};
        request.utf8FileName = name;
        request.fileSize = bytes.size();
        request.head = std::span<const std::byte>{bytes}.first(32);

        const auto opened = plugin->open(request);
        const std::string detail = opened ? std::string{} : opened.error().detail;
        CAPTURE(detail);
        REQUIRE(opened.has_value());
        CHECK((*opened)->imageInfo().formatName == "SVG");
        CHECK((*opened)->pageInfo(0).has_value());
    }

    TEST_CASE("SVG composed disk open reads the full source after a comment-only head")
    {
        const std::string headText = "<?xml version=\"1.0\"?>\n<!-- head ends -->\n";
        std::string document = headText;
        document.append(5000, ' ');
        document += "<!-- root follows the supplied head -->\n"
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"/>";
        const TemporarySvgFile file{"comment", document};
        const auto plugin = pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        const auto bytes = textBytes(document);
        const std::string name = utf8Path(file.path());
        pvd::OpenRequest request{};
        request.utf8FileName = name;
        request.fileSize = bytes.size();
        request.head = std::span<const std::byte>{bytes}.first(headText.size());

        const auto opened = plugin->open(request);
        const std::string detail = opened ? std::string{} : opened.error().detail;
        CAPTURE(detail);
        REQUIRE(opened.has_value());
        CHECK((*opened)->imageInfo().formatName == "SVG");
        CHECK((*opened)->pageInfo(0).has_value());
    }

    TEST_CASE("SVG composed open still rejects HTML non-SVG XML and SVGX candidates")
    {
        const auto plugin = pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        for (const std::string_view text : {"<html><body/></html>", "<?xml version=\"1.0\"?><root/>", "<svgx/>"}) {
            CAPTURE(text);
            auto bytes = textBytes(text);
            pvd::OpenRequest request{};
            request.head = bytes;
            CHECK_FALSE(plugin->open(request).has_value());
        }
    }

    TEST_CASE("SVG DecoderFactory creates one static RGBA8 page for a valid SVG")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = readFixture("basic-2x2-colors.svg");
        REQUIRE(factory.recognises(bytes));
        const auto source = fixturePath("basic-2x2-colors.svg");
        const auto decoder = requireDecoder(factory, bytes, testOptions(source));

        const auto &meta = decoder->meta();
        CHECK(meta.width == 2);
        CHECK(meta.height == 2);
        CHECK(meta.depth == 8);
        CHECK(meta.hasAlpha);
        CHECK(meta.frameCount == 1);
        CHECK_FALSE(meta.animated);
    }

    TEST_CASE("SVG DecoderFactory rejects an empty source before resvg")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto empty = std::vector<std::byte>{};

        const auto created = factory.create(empty, unlimitedOptions());
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::ParseFailed);
        CHECK_FALSE(created.error().detail.empty());
    }

    TEST_CASE("SVG decoder reports one zero-length frame and rejects page one")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto decoder = requireDecoder(factory, sizedSvg("1", "1"), unlimitedOptions());

        const auto first = decoder->frameTiming(0);
        REQUIRE(first.has_value());
        CHECK(first->durationMs == 0);

        const auto second = decoder->frameTiming(1);
        REQUIRE_FALSE(second.has_value());
        CHECK(second.error().code == ErrorCode::PageOutOfRange);
    }

    TEST_CASE("SVG accepted fixtures render one page at their analytic samples")
    {
        SvgState state;
        DecoderFactory factory{state};
        for (const auto &fixture : acceptedFixtures()) {
            CAPTURE(fixture.name);
            const auto bytes = readFixture(fixture.name);
            const auto source = fixturePath(fixture.name);
            const auto decoder = requireDecoder(factory, bytes, testOptions(source));
            const auto &meta = decoder->meta();
            CHECK(meta.frameCount == 1);
            CHECK_FALSE(meta.animated);
            CHECK(meta.width == fixture.width);
            CHECK(meta.height == fixture.height);
            CHECK(meta.depth == 8);
            CHECK(meta.hasAlpha);
            const auto pixels = render(*decoder, pvd::PixelFormat::Rgba32);
            checkSamples(fixture, pixels);
        }
    }

    TEST_CASE("SVG output is straight alpha in RGBA/BGRA and RGB/BGR")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto rampBytes = readFixture("alpha-ramp.svg");
        const auto rampSource = fixturePath("alpha-ramp.svg");
        const auto ramp = requireDecoder(factory, rampBytes, testOptions(rampSource));
        const auto rgba = render(*ramp, pvd::PixelFormat::Rgba32);
        const auto bgra = render(*ramp, pvd::PixelFormat::Bgra32);
        CHECK(pixel(rgba, 3, 4, 1, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 128});
        CHECK(pixel(bgra, 3, 4, 1, 0) == std::array<std::uint8_t, 4>{0, 0, 255, 128});
        CHECK(pixel(rgba, 3, 4, 0, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 0});

        const auto basicBytes = readFixture("basic-2x2-colors.svg");
        const auto basicSource = fixturePath("basic-2x2-colors.svg");
        const auto basic = requireDecoder(factory, basicBytes, testOptions(basicSource));
        const auto rgb = render(*basic, pvd::PixelFormat::Rgb24);
        const auto bgr = render(*basic, pvd::PixelFormat::Bgr24);
        CHECK(pixel(rgb, 2, 3, 0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        CHECK(pixel(bgr, 2, 3, 0, 0) == std::array<std::uint8_t, 4>{0, 0, 255, 255});

        const auto repeated = render(*basic, pvd::PixelFormat::Rgba32);
        const auto again = render(*basic, pvd::PixelFormat::Rgba32);
        CHECK(repeated == again);
    }

    TEST_CASE("SVGZ decodes identically to its uncompressed source")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto svgBytes = readFixture("basic-2x2-colors.svg");
        const auto svgzBytes = readFixture("valid.svgz");
        const auto svgSource = fixturePath("basic-2x2-colors.svg");
        const auto svgzSource = fixturePath("valid.svgz");
        const auto svg = requireDecoder(factory, svgBytes, testOptions(svgSource));
        const auto svgz = requireDecoder(factory, svgzBytes, testOptions(svgzSource));
        CHECK(render(*svg, pvd::PixelFormat::Rgba32) == render(*svgz, pvd::PixelFormat::Rgba32));
    }

    TEST_CASE("SVG rejects malformed XML non-SVG invalid UTF-8 and broken gzip")
    {
        SvgState state;
        DecoderFactory factory{state};
        for (const auto name : {"malformed.svg", "not-svg.svg", "invalid-utf8.svg", "truncated.svgz"}) {
            CAPTURE(name);
            const auto bytes = readFixture(name);
            CHECK_FALSE(factory.create(bytes, testOptions()).has_value());
        }
    }

    TEST_CASE("SVG intrinsic and pixel limits reject oversized input")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto tooWide = textBytes("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"101\" height=\"1\"/>");
        auto dimensionOptions = testOptions();
        dimensionOptions.maxDimension = 100;
        const auto dimension = factory.create(tooWide, dimensionOptions);
        REQUIRE_FALSE(dimension.has_value());
        CHECK(dimension.error().code == ErrorCode::TooLarge);

        const auto tooManyPixels = textBytes("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"11\" height=\"10\"/>");
        auto pixelOptions = testOptions();
        pixelOptions.maxPixels = 109;
        const auto pixels = factory.create(tooManyPixels, pixelOptions);
        REQUIRE_FALSE(pixels.has_value());
        CHECK(pixels.error().code == ErrorCode::TooLarge);
    }

    TEST_CASE("SVG factory rejects a filled path above tiny-skia's signed height")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = textBytes("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"2147483648\" "
                                     "viewBox=\"0 0 1 1\"><path d=\"M0 0h1v1h-1z\" fill=\"#123456\"/></svg>");

        const auto created = factory.create(bytes, unlimitedOptions());
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::TooLarge);
    }

    TEST_CASE("SVG Cyrillic-only text has system-font ink and removing it removes ink")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = readFixture("text-cyrillic.svg");
        const auto source = fixturePath("text-cyrillic.svg");
        const auto decoder = requireDecoder(factory, bytes, testOptions(source));
        const auto pixels = render(*decoder, pvd::PixelFormat::Rgba32);
        CHECK(darkPixelCount(pixels, decoder->meta().width, decoder->meta().height) > 0);

        std::string text{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
        const auto textStart = text.find("<text");
        const auto textEnd = text.find("</text>");
        REQUIRE(textStart != std::string::npos);
        REQUIRE(textEnd != std::string::npos);
        text.erase(textStart, textEnd - textStart + std::string_view{"</text>"}.size());
        const auto withoutText = textBytes(text);
        const auto blank = requireDecoder(factory, withoutText, testOptions(source));
        const auto blankPixels = render(*blank, pvd::PixelFormat::Rgba32);
        CHECK(darkPixelCount(blankPixels, blank->meta().width, blank->meta().height) == 0);
    }

    TEST_CASE("SVG relative image resolves from a Unicode absolute source path")
    {
        const UnicodeFixtureCopy copy;
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = readFixture("image-relative.svg");
        const auto source = copy.svg().wstring();
        const auto decoder = requireDecoder(factory, bytes, testOptions(source));
        const auto pixels = render(*decoder, pvd::PixelFormat::Rgba32);
        const std::array<Sample, 4> samples{{{0, 0, {255, 0, 0, 255}, 0},
                                             {1, 0, {0, 255, 0, 255}, 0},
                                             {0, 1, {0, 0, 255, 255}, 0},
                                             {1, 1, {255, 255, 0, 255}, 0}}};
        for (const auto &sample : samples) {
            CHECK(pixel(pixels, 2, 4, sample.x, sample.y) == sample.rgba);
        }
    }

    TEST_CASE("SVG invalid page unsupported format and destination bounds are rejected")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = readFixture("basic-2x2-colors.svg");
        const auto source = fixturePath("basic-2x2-colors.svg");
        const auto decoder = requireDecoder(factory, bytes, testOptions(source));
        const auto &meta = decoder->meta();
        const auto pitch = static_cast<std::uint32_t>(meta.width * 4);
        std::vector<std::byte> pixels(static_cast<std::size_t>(pitch) * meta.height);

        const auto page = decoder->decodeFrame(1, pvd::PixelFormat::Rgba32, pixels, pitch);
        REQUIRE_FALSE(page.has_value());
        CHECK(page.error().code == ErrorCode::PageOutOfRange);

        const auto deep = decoder->decodeFrame(0, pvd::PixelFormat::Rgba64, pixels, pitch);
        REQUIRE_FALSE(deep.has_value());
        CHECK(deep.error().code == ErrorCode::UnsupportedFeature);

        CHECK_FALSE(decoder->decodeFrame(0, pvd::PixelFormat::Rgba32, pixels, pitch - 1).has_value());
        CHECK_FALSE(decoder->decodeFrame(0, pvd::PixelFormat::Rgba32, std::span{pixels}.first(pixels.size() - 1), pitch)
                        .has_value());
    }

    TEST_CASE("SVG decoded pages outlive their input bytes and independent sessions coexist")
    {
        SvgState state;
        DecoderFactory factory{state};
        auto bytes = readFixture("basic-2x2-colors.svg");
        const auto source = fixturePath("basic-2x2-colors.svg");
        const auto first = requireDecoder(factory, bytes, testOptions(source));
        bytes.assign(bytes.size(), std::byte{0});

        const auto rampBytes = readFixture("alpha-ramp.svg");
        const auto rampSource = fixturePath("alpha-ramp.svg");
        const auto second = requireDecoder(factory, rampBytes, testOptions(rampSource));

        const auto firstPixels = render(*first, pvd::PixelFormat::Rgba32);
        const auto secondPixels = render(*second, pvd::PixelFormat::Rgba32);
        CHECK(pixel(firstPixels, 2, 4, 0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        CHECK(pixel(secondPixels, 3, 4, 1, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 128});
    }

    TEST_CASE("SVG fractional intrinsic size is rounded to a one-pixel page")
    {
        SvgState state;
        DecoderFactory factory{state};
        for (const auto &[width, height] : std::array{std::pair<std::string_view, std::string_view>{"0.1", "1"},
                                                      std::pair<std::string_view, std::string_view>{"1", "0.1"}}) {
            CAPTURE(width);
            CAPTURE(height);
            std::string text = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"";
            text.append(width);
            text.append("\" height=\"");
            text.append(height);
            text.append("\" viewBox=\"0 0 1 1\"><rect width=\"1\" height=\"1\" fill=\"#123456\"/></svg>");

            const auto created = factory.create(textBytes(text), unlimitedOptions());
            const std::string detail = created ? std::string{} : created.error().detail;
            CAPTURE(detail);
            REQUIRE(created.has_value());
            const auto &meta = (*created)->meta();
            CHECK(meta.width == 1);
            CHECK(meta.height == 1);

            std::array<std::byte, 4> pixelBytes{};
            const auto decoded = (*created)->decodeFrame(0, pvd::PixelFormat::Rgba32, pixelBytes, 4);
            REQUIRE(decoded.has_value());
        }
    }

    TEST_CASE("SVG policy limits accept exact safe boundaries")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = sizedSvg("2", "2");

        auto options = unlimitedOptions();
        options.maxDimension = 2;
        options.maxPixels = 4;
        const auto atLimits = factory.create(bytes, options);
        REQUIRE(atLimits.has_value());
        CHECK((*atLimits)->meta().width == 2);
        CHECK((*atLimits)->meta().height == 2);

        options.maxDimension = 1;
        const auto dimension = factory.create(bytes, options);
        REQUIRE_FALSE(dimension.has_value());
        CHECK(dimension.error().code == ErrorCode::TooLarge);

        options.maxDimension = 2;
        options.maxPixels = 3;
        const auto pixels = factory.create(bytes, options);
        REQUIRE_FALSE(pixels.has_value());
        CHECK(pixels.error().code == ErrorCode::TooLarge);
    }

    TEST_CASE("SVG zero policy limits still enforce renderer and addressability caps")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto options = unlimitedOptions();
        CHECK(options.maxDimension == 0);
        CHECK(options.maxPixels == 0);

        for (const auto &[width, height] :
             std::array{std::pair<std::string_view, std::string_view>{"536870912", "1"},
                        std::pair<std::string_view, std::string_view>{"2147483648", "1"},
                        std::pair<std::string_view, std::string_view>{"2147483648", "2147483648"}}) {
            CAPTURE(width);
            CAPTURE(height);
            const auto created = factory.create(sizedSvg(width, height), options);
            if (created) {
                CHECK_FALSE(created.has_value());
            } else {
                CHECK(created.error().code == ErrorCode::TooLarge);
            }
        }
    }

    TEST_CASE("SVG source path with a lone surrogate returns ConversionFailed without throwing")
    {
        SvgState state;
        DecoderFactory factory{state};
        const auto bytes = sizedSvg("1", "1");
        std::wstring sourcePath = L"C:\\pvdkit-surrogate-";
        sourcePath.push_back(static_cast<wchar_t>(0xd800));
        sourcePath += L"\\image.svg";
        auto options = unlimitedOptions();
        options.sourcePath = std::wstring_view{sourcePath};

        std::optional<core::Result<std::unique_ptr<core::IDecoder>>> result;
        CHECK_NOTHROW([&] { result.emplace(factory.create(bytes, options)); }());
        REQUIRE(result.has_value());
        REQUIRE_FALSE((*result).has_value());
        CHECK((*result).error().code == ErrorCode::ConversionFailed);
    }

    TEST_CASE("SVG DecoderFactory is neither copyable nor movable")
    {
        CHECK_FALSE(std::is_copy_constructible_v<DecoderFactory>);
        CHECK_FALSE(std::is_copy_assignable_v<DecoderFactory>);
        CHECK_FALSE(std::is_move_constructible_v<DecoderFactory>);
        CHECK_FALSE(std::is_move_assignable_v<DecoderFactory>);
    }

} // namespace pvdkit::svg::tests
