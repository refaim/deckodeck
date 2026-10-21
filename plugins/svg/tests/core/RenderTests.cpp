#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "core/Render.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::svg
{
    namespace
    {

        core::DecoderOptions unlimitedOptions()
        {
            core::DecoderOptions options{};
            options.maxDimension = 0;
            options.maxPixels = 0;
            return options;
        }

        core::ImageMeta sizeMeta(const std::uint32_t width, const std::uint32_t height)
        {
            core::ImageMeta meta{};
            meta.width = width;
            meta.height = height;
            return meta;
        }

        std::vector<std::byte> bytes(const std::initializer_list<std::uint8_t> values)
        {
            std::vector<std::byte> result;
            result.reserve(values.size());
            for (const auto value : values) {
                result.push_back(static_cast<std::byte>(value));
            }
            return result;
        }

    } // namespace

    TEST_CASE("SVG raster planning applies the upstream fractional-size rule")
    {
        const auto options = unlimitedOptions();
        for (const auto &[width, height, expectedWidth, expectedHeight] :
             std::array{std::tuple{0.1F, 1.0F, 1U, 1U}, std::tuple{1.0F, 0.1F, 1U, 1U}, std::tuple{2.4F, 3.6F, 2U, 4U},
                        std::tuple{2.5F, 3.5F, 3U, 4U}}) {
            CAPTURE(width);
            CAPTURE(height);
            const auto plan = planRaster(width, height, options);
            REQUIRE(plan.has_value());
            CHECK(plan->meta.width == expectedWidth);
            CHECK(plan->meta.height == expectedHeight);
            CHECK(plan->meta.depth == 8);
            CHECK(plan->meta.chroma == core::ChromaFormat::Yuv444);
            CHECK(plan->meta.hasAlpha);
            CHECK(plan->meta.frameCount == 1);
            CHECK(plan->meta.cicp.primaries == 1);
            CHECK(plan->meta.cicp.transfer == 13);
            CHECK(plan->meta.cicp.matrix == 0);
            CHECK(plan->meta.cicp.fullRange);
            CHECK(plan->rasterBytes == static_cast<std::size_t>(plan->meta.width) * plan->meta.height * 4U);
        }
    }

    TEST_CASE("SVG raster plan default initialization starts at zero bytes")
    {
        RasterPlan plan;
        CHECK(plan.rasterBytes == 0);
    }

    TEST_CASE("SVG raster planning rejects non-finite and non-positive intrinsic sizes")
    {
        const auto options = unlimitedOptions();
        const auto invalid = [&](const float width, const float height) {
            const auto plan = planRaster(width, height, options);
            REQUIRE_FALSE(plan.has_value());
            CHECK(plan.error().code == core::ErrorCode::ParseFailed);
            CHECK_FALSE(plan.error().detail.empty());
        };

        invalid(std::numeric_limits<float>::quiet_NaN(), 1.0F);
        invalid(1.0F, std::numeric_limits<float>::quiet_NaN());
        invalid(std::numeric_limits<float>::infinity(), 1.0F);
        invalid(1.0F, std::numeric_limits<float>::infinity());
        invalid(-std::numeric_limits<float>::infinity(), 1.0F);
        invalid(1.0F, -std::numeric_limits<float>::infinity());
        invalid(0.0F, 1.0F);
        invalid(1.0F, 0.0F);
        invalid(-1.0F, 1.0F);
        invalid(1.0F, -1.0F);
    }

    TEST_CASE("SVG raster planning rejects sizes beyond the renderer and address space")
    {
        const auto options = unlimitedOptions();
        constexpr auto kWidthLimit = 536'870'911U;

        const auto wide = planRaster(static_cast<float>(kWidthLimit + 1U), 1.0F, options);
        REQUIRE_FALSE(wide.has_value());
        CHECK(wide.error().code == core::ErrorCode::TooLarge);

        const auto atRendererLimit = planRaster(536'870'848.0F, 1.0F, options);
        REQUIRE(atRendererLimit.has_value());
        CHECK(atRendererLimit->meta.width == 536'870'848U);

        const auto narrow =
            planRaster(1.0F, static_cast<float>(std::numeric_limits<std::uint32_t>::max()) + 1.0F, options);
        REQUIRE_FALSE(narrow.has_value());
        CHECK(narrow.error().code == core::ErrorCode::TooLarge);

        const auto huge = planRaster(1.0E30F, 1.0F, options);
        REQUIRE_FALSE(huge.has_value());
        CHECK(huge.error().code == core::ErrorCode::TooLarge);
    }

    TEST_CASE("SVG raster planning rejects heights beyond tiny-skia's signed geometry")
    {
        const auto options = unlimitedOptions();

        const auto atSignedGeometry = planRaster(1.0F, 2'147'483'520.0F, options);
        if constexpr (sizeof(std::size_t) == 4) {
            REQUIRE_FALSE(atSignedGeometry.has_value());
            CHECK(atSignedGeometry.error().code == core::ErrorCode::TooLarge);
        } else {
            REQUIRE(atSignedGeometry.has_value());
            CHECK(atSignedGeometry->meta.width == 1);
            CHECK(atSignedGeometry->meta.height == 2'147'483'520U);
            CHECK(atSignedGeometry->rasterBytes == 8'589'934'080ULL);
        }

        const auto beyondSignedGeometry = planRaster(1.0F, 2'147'483'648.0F, options);
        REQUIRE_FALSE(beyondSignedGeometry.has_value());
        CHECK(beyondSignedGeometry.error().code == core::ErrorCode::TooLarge);
    }

    TEST_CASE("SVG raster planning enforces Rust's signed addressable byte limit")
    {
        const auto options = unlimitedOptions();

        const auto twoGiB = planRaster(16'384.0F, 32'768.0F, options);
        if constexpr (sizeof(std::size_t) == 4) {
            REQUIRE_FALSE(twoGiB.has_value());
            CHECK(twoGiB.error().code == core::ErrorCode::TooLarge);
        } else {
            REQUIRE(twoGiB.has_value());
            CHECK(twoGiB->meta.width == 16'384);
            CHECK(twoGiB->meta.height == 32'768);
            CHECK(twoGiB->rasterBytes == std::size_t{1} << 31U);
        }

        const auto belowSignedByteLimit = planRaster(16'384.0F, 32'767.0F, options);
        REQUIRE(belowSignedByteLimit.has_value());
        CHECK(belowSignedByteLimit->meta.width == 16'384);
        CHECK(belowSignedByteLimit->meta.height == 32'767);
        CHECK(belowSignedByteLimit->rasterBytes == std::size_t{2'147'418'112});
    }

    TEST_CASE("SVG decoder policy limits apply exact boundaries and treat zero as unlimited")
    {
        const auto bytesAtLimit = planRaster(2.0F, 2.0F, unlimitedOptions());
        REQUIRE(bytesAtLimit.has_value());

        auto options = unlimitedOptions();
        options.maxDimension = 2;
        options.maxPixels = 4;
        const auto atLimits = planRaster(2.0F, 2.0F, options);
        REQUIRE(atLimits.has_value());

        options.maxDimension = 1;
        const auto dimension = planRaster(2.0F, 2.0F, options);
        REQUIRE_FALSE(dimension.has_value());
        CHECK(dimension.error().code == core::ErrorCode::TooLarge);

        options.maxDimension = 1;
        const auto tall = planRaster(1.0F, 2.0F, options);
        REQUIRE_FALSE(tall.has_value());
        CHECK(tall.error().code == core::ErrorCode::TooLarge);

        options.maxDimension = 2;
        options.maxPixels = 3;
        const auto pixels = planRaster(2.0F, 2.0F, options);
        REQUIRE_FALSE(pixels.has_value());
        CHECK(pixels.error().code == core::ErrorCode::TooLarge);

        options.maxPixels = 4;
        const auto exactPixels = planRaster(2.0F, 2.0F, options);
        REQUIRE(exactPixels.has_value());
    }

    TEST_CASE("SVG destination validation rejects deep formats and broken layouts")
    {
        const auto meta = sizeMeta(2, 2);
        for (const auto deep : {pvd::PixelFormat::Bgra64, pvd::PixelFormat::Rgba64, pvd::PixelFormat::Rgb48}) {
            const auto result = checkDestination(meta, deep, 64, 16);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code == core::ErrorCode::UnsupportedFeature);
        }

        const auto badPitch = checkDestination(meta, pvd::PixelFormat::Rgba32, 16, 7);
        REQUIRE_FALSE(badPitch.has_value());
        CHECK(badPitch.error().code == core::ErrorCode::Internal);

        const auto badSize = checkDestination(meta, pvd::PixelFormat::Rgba32, 15, 8);
        REQUIRE_FALSE(badSize.has_value());
        CHECK(badSize.error().code == core::ErrorCode::Internal);

        CHECK(checkDestination(meta, pvd::PixelFormat::Rgba32, 16, 8).has_value());
        CHECK(checkDestination(meta, pvd::PixelFormat::Rgb24, 12, 6).has_value());
    }

    TEST_CASE("SVG premultiplied conversion produces straight alpha in every host byte order")
    {
        const auto source = bytes({128, 64, 32, 128});
        CHECK(source.size() == 4);

        std::array<std::byte, 4> rgba{};
        convertPremultiplied(source, 1, 1, pvd::PixelFormat::Rgba32, rgba, 4);
        CHECK(rgba == std::array{std::byte{255}, std::byte{128}, std::byte{64}, std::byte{128}});

        std::array<std::byte, 4> bgra{};
        convertPremultiplied(source, 1, 1, pvd::PixelFormat::Bgra32, bgra, 4);
        CHECK(bgra == std::array{std::byte{64}, std::byte{128}, std::byte{255}, std::byte{128}});

        std::array<std::byte, 3> rgb{};
        convertPremultiplied(source, 1, 1, pvd::PixelFormat::Rgb24, rgb, 3);
        CHECK(rgb == std::array{std::byte{255}, std::byte{128}, std::byte{64}});

        std::array<std::byte, 3> bgr{};
        convertPremultiplied(source, 1, 1, pvd::PixelFormat::Bgr24, bgr, 3);
        CHECK(bgr == std::array{std::byte{64}, std::byte{128}, std::byte{255}});

        const auto transparent = bytes({10, 20, 30, 0});
        std::array<std::byte, 4> transparentRgba{std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        convertPremultiplied(transparent, 1, 1, pvd::PixelFormat::Rgba32, transparentRgba, 4);
        CHECK(transparentRgba == std::array{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});

        const auto saturated = bytes({255, 128, 64, 1});
        std::array<std::byte, 4> clamped{};
        convertPremultiplied(saturated, 1, 1, pvd::PixelFormat::Rgba32, clamped, 4);
        CHECK(clamped == std::array{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{1}});

        std::vector<std::byte> rows(sizeof(std::uint32_t) * 2U);
        const auto two = bytes({128, 64, 32, 128, 64, 32, 16, 64});
        convertPremultiplied(two, 2, 1, pvd::PixelFormat::Rgba32, rows, 8);
        CHECK(std::to_integer<std::uint8_t>(rows[0]) == 255);
        CHECK(std::to_integer<std::uint8_t>(rows[4]) == 255);
        CHECK(std::to_integer<std::uint8_t>(rows[5]) == 128);
        CHECK(std::to_integer<std::uint8_t>(rows[6]) == 64);
        CHECK(std::to_integer<std::uint8_t>(rows[7]) == 64);
    }

} // namespace pvdkit::svg
