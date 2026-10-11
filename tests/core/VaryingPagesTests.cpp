#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "Fakes.hpp"
#include "core/FileSession.hpp"

namespace pvdkit::core
{
    namespace
    {
        struct PagesState
        {
            std::vector<PageMeta> pages;
            std::vector<ColourEncoding> colours;
            std::optional<Error> metadataError{};
            std::optional<Error> decodeError{};
            unsigned colourReads = 0;
            unsigned decodes = 0;
            bool alterFileMetaAfterDecode = false;
        };

        class PagesDecoder final : public IDecoder
        {
          public:
            explicit PagesDecoder(PagesState &state) : state_(state), fileMeta_(state.pages.front().image)
            {
                fileMeta_.frameCount = static_cast<std::uint32_t>(state.pages.size());
            }
            const ImageMeta &meta() const override
            {
                return fileMeta_;
            }
            Result<PageMeta> pageMeta(const std::uint32_t page) const override
            {
                if (state_.metadataError) {
                    return std::unexpected(*state_.metadataError);
                }
                if (page >= state_.pages.size()) {
                    return std::unexpected(test::error(ErrorCode::PageOutOfRange));
                }
                return state_.pages[page];
            }
            ColourEncoding decodedColour() const override
            {
                ++state_.colourReads;
                return state_.colours[lastPage_];
            }
            Result<FrameTiming> frameTiming(std::uint32_t) const override
            {
                return FrameTiming{789};
            }
            Result<void> decodeFrame(const std::uint32_t page, const pvd::PixelFormat format,
                                     const std::span<std::byte> dst, const std::uint32_t pitch) override
            {
                ++state_.decodes;
                if (state_.decodeError) {
                    return std::unexpected(*state_.decodeError);
                }
                const auto &header = state_.pages[page];
                const auto layout = pvd::pixelLayout(format);
                if (pitch != header.image.width * layout.bytesPerPixel ||
                    dst.size() != static_cast<std::size_t>(pitch) * header.image.height) {
                    return std::unexpected(test::error(ErrorCode::ConversionFailed, "wrong page allocation"));
                }
                const bool wide = format == pvd::PixelFormat::Rgb48 || format == pvd::PixelFormat::Rgba64 ||
                                  format == pvd::PixelFormat::Bgra64;
                for (std::size_t offset = 0; offset < dst.size(); ++offset) {
                    dst[offset] =
                        wide ? std::byte{static_cast<unsigned char>((offset % 2) * 128U)}
                             : std::byte{static_cast<unsigned char>(1U + page * 10U + offset / layout.bytesPerPixel)};
                }
                lastPage_ = page;
                if (state_.alterFileMetaAfterDecode) {
                    fileMeta_.width = 100;
                    fileMeta_.height = 200;
                    fileMeta_.hasAlpha = !fileMeta_.hasAlpha;
                    fileMeta_.transforms.irotAngle = 1;
                }
                return {};
            }

          private:
            PagesState &state_;
            ImageMeta fileMeta_;
            std::uint32_t lastPage_ = 0;
        };

        PagesState pagesState()
        {
            return PagesState{{PageMeta{test::meta(3, 2, false, 8)}, PageMeta{test::meta(1, 3, true, 16)}},
                              {{Cicp{1, 13, 0, true}, {}, {}}, {Cicp{1, 13, 0, true}, {}, {}}}};
        }

        TEST_CASE("varying pages report their own dimensions alpha and source depth")
        {
            auto state = pagesState();
            const auto meta = test::meta(3, 2, false, 8, 2);
            FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(meta), test::options(),
                                test::outputTables());
            const auto info = session.pageInfo(1);
            REQUIRE(info.has_value());
            CHECK(info->width == 1);
            CHECK(info->height == 3);
            CHECK(info->bitsPerPixel == 64);
            CHECK(info->frameTimeMs == 0);
            CHECK_FALSE(session.imageInfo().animated);
            CHECK(session.imageInfo().pageCount == 2);
        }

        TEST_CASE("varying pages allocate independently and preserve both outstanding views")
        {
            for (const bool native : {false, true}) {
                for (const bool reverse : {false, true}) {
                    auto state = pagesState();
                    auto options = test::options();
                    options.nativeRgbOutput = native;
                    options.deepOutput = true;
                    FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(test::meta()),
                                        options, test::outputTables());
                    const auto first = session.decodePage(0, pvd::Progress{});
                    REQUIRE(first.has_value());
                    const std::vector saved(first->pixels.begin(), first->pixels.end());
                    const auto second = session.decodePage(1, pvd::Progress{});
                    REQUIRE(second.has_value());
                    CHECK(second->pitchBytes == 8);
                    CHECK(second->pixels.size() == 24);
                    CHECK(second->hasAlpha);
                    CHECK(second->bitsPerPixel == (native ? 65 : 64));
                    CHECK(first->pixels.data() != second->pixels.data());
                    CHECK(std::vector(first->pixels.begin(), first->pixels.end()) == saved);
                    CHECK(session.freePage(reverse ? second->pixels : first->pixels));
                    CHECK(session.freePage(reverse ? first->pixels : second->pixels));
                }
            }
        }

        TEST_CASE("varying pages apply only their own transforms and host orientation")
        {
            auto state = pagesState();
            state.pages[1].image = test::meta(1, 3, true, 8);
            state.pages[1].image.exifOrientation = 6;
            state.pages[1].image.transforms.irotAngle = 1;
            FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(test::meta()),
                                test::options(), test::outputTables());
            const auto info = session.pageInfo(1);
            REQUIRE(info.has_value());
            CHECK(info->width == 3);
            CHECK(info->height == 1);
            const auto decoded = session.decodePage(1, pvd::Progress{});
            REQUIRE(decoded.has_value());
            CHECK(decoded->pitchBytes == 12);
            CHECK(decoded->pixels.size() == 12);
            CHECK(decoded->hostOrientation == 0);
            CHECK(decoded->pixels[0] == std::byte{11});
            CHECK(decoded->pixels[4] == std::byte{12});
            CHECK(decoded->pixels[8] == std::byte{13});
        }

        TEST_CASE("varying page metadata failures propagate before allocation or decode")
        {
            auto state = pagesState();
            state.metadataError = test::error(ErrorCode::UnsupportedFeature, "unsupported page header");
            FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(test::meta()),
                                test::options(), test::outputTables());
            const auto info = session.pageInfo(1);
            REQUIRE_FALSE(info.has_value());
            CHECK(info.error().detail == "unsupported page header");
            const auto decoded = session.decodePage(1, pvd::Progress{});
            REQUIRE_FALSE(decoded.has_value());
            CHECK(decoded.error().code == ErrorCode::UnsupportedFeature);
            CHECK(decoded.error().detail == "unsupported page header");
            CHECK(state.decodes == 0);
            CHECK(state.colourReads == 0);
        }

        TEST_CASE("varying pages force precision for late colour and use successfully decoded encoding")
        {
            for (const bool native : {false, true}) {
                auto state = pagesState();
                state.pages[0].forceHighPrecision = true;
                state.colours[0].cicp = Cicp{1, 8, 0, true};
                auto options = test::options();
                options.nativeRgbOutput = native;
                FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(test::meta()),
                                    options, test::outputTables());
                const auto decoded = session.decodePage(0, pvd::Progress{});
                REQUIRE(decoded.has_value());
                CHECK(decoded->bitsPerPixel == (native ? 49 : 64));
                CHECK(decoded->pitchBytes == (native ? 18 : 24));
                CHECK_FALSE(decoded->hasAlpha);
                // Linear 0x8000 must become sRGB 48192; using the header's sRGB would leave 32768.
                CHECK(decoded->pixels[0] == std::byte{0x40});
                CHECK(decoded->pixels[1] == std::byte{0xBC});
                CHECK(state.colourReads == 1);
            }
        }

        TEST_CASE("varying pages decode errors do not consume stale colour and allow retry")
        {
            auto state = pagesState();
            state.pages[0].forceHighPrecision = true;
            state.colours[0].cicp = Cicp{1, 8, 0, true};
            state.decodeError = test::error(ErrorCode::DecodeFailed, "lazy page broken");
            FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(test::meta()),
                                test::options(), test::outputTables());
            const auto failed = session.decodePage(0, pvd::Progress{});
            REQUIRE_FALSE(failed.has_value());
            CHECK(failed.error().detail == "lazy page broken");
            CHECK(state.colourReads == 0);
            state.decodeError.reset();
            const auto retried = session.decodePage(0, pvd::Progress{});
            REQUIRE(retried.has_value());
            CHECK(retried->pixels[0] == std::byte{0x40});
            CHECK(retried->pixels[1] == std::byte{0xBC});
        }

        TEST_CASE("varying page decoded colour never changes allocated geometry or transforms")
        {
            auto state = pagesState();
            state.alterFileMetaAfterDecode = true;
            FileSession session(nullptr, std::make_unique<PagesDecoder>(state), test::imageInfo(test::meta()),
                                test::options(), test::outputTables());
            const auto decoded = session.decodePage(0, pvd::Progress{});
            REQUIRE(decoded.has_value());
            CHECK(decoded->pitchBytes == 9);
            CHECK(decoded->pixels.size() == 18);
            CHECK_FALSE(decoded->hasAlpha);
            CHECK(decoded->hostOrientation == 0);
        }
    } // namespace
} // namespace pvdkit::core
