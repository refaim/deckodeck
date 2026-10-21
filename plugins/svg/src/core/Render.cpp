#include "core/Render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace pvdkit::svg
{
    namespace
    {

        constexpr std::size_t kRgbaBytes = 4;
        // tiny-skia's min_row_bytes() converts the width to i32 and multiplies by four; a larger
        // width makes PixmapMut::from_bytes return None, which resvg_render unwraps.
        constexpr std::uint32_t kMaxRenderWidth =
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max() / 4);
        // Filled paths build tiny-skia IntRect geometry, whose height is an i32.
        constexpr std::uint32_t kMaxRenderHeight = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());

        static_assert(static_cast<std::uint64_t>(kMaxRenderWidth) * kMaxRenderHeight <=
                          std::numeric_limits<std::uint64_t>::max() / kRgbaBytes,
                      "bounded SVG dimensions keep the raster byte count in uint64");

        // On 64-bit these same bounds prove the byte count fits ptrdiff_t; only the 32-bit build
        // needs a runtime signed-byte check.
        static_assert(sizeof(std::size_t) < sizeof(std::uint64_t) ||
                          static_cast<std::uint64_t>(kMaxRenderWidth) * kMaxRenderHeight <=
                              static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / kRgbaBytes,
                      "64-bit builds prove the bounded raster byte count fits ptrdiff_t");

        core::Error invalidSize()
        {
            return {core::ErrorCode::ParseFailed, "resvg produced an empty or non-finite image size"};
        }

        core::Error tooLarge(const std::string_view what)
        {
            return {core::ErrorCode::TooLarge, std::string{what}};
        }

        core::Result<std::uint32_t> dimension(const float value, const std::uint32_t limit, const std::string_view axis)
        {
            if (!std::isfinite(value) || value <= 0.0F) {
                return std::unexpected(invalidSize());
            }
            const double rounded = std::round(static_cast<double>(value));
            if (rounded > static_cast<double>(limit)) {
                return std::unexpected(tooLarge("the SVG intrinsic " + std::string{axis} + " is too large"));
            }
            return static_cast<std::uint32_t>(std::max(1.0, rounded));
        }

    } // namespace

    core::Result<RasterPlan> planRaster(const float width, const float height, const core::DecoderOptions &options)
    {
        const auto plannedWidth = dimension(width, kMaxRenderWidth, "width");
        if (!plannedWidth) {
            return std::unexpected(plannedWidth.error());
        }
        const auto plannedHeight = dimension(height, kMaxRenderHeight, "height");
        if (!plannedHeight) {
            return std::unexpected(plannedHeight.error());
        }

        if (options.maxDimension != 0 &&
            (*plannedWidth > options.maxDimension || *plannedHeight > options.maxDimension)) {
            return std::unexpected(tooLarge("the SVG intrinsic size exceeds the dimension limit"));
        }
        const auto pixelCount = static_cast<std::uint64_t>(*plannedWidth) * *plannedHeight;
        if (options.maxPixels != 0 && pixelCount > options.maxPixels) {
            return std::unexpected(tooLarge("the SVG pixel count exceeds the pixel limit"));
        }

        // The static assertions above prove the uint64 product. On 32-bit the raster must also fit
        // Rust's signed byte count (from_raw_parts_mut takes isize), which is below SIZE_MAX.
        const auto rasterBytes64 = pixelCount * kRgbaBytes;
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
            if (rasterBytes64 > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
                return std::unexpected(tooLarge("the SVG raster exceeds the 32-bit signed byte limit"));
            }
        }
        const auto rasterBytes = static_cast<std::size_t>(rasterBytes64);

        core::ImageMeta meta{};
        meta.width = *plannedWidth;
        meta.height = *plannedHeight;
        meta.depth = 8;
        meta.chroma = core::ChromaFormat::Yuv444;
        meta.hasAlpha = true;
        meta.cicp = core::Cicp{1, 13, 0, true};
        meta.frameCount = 1;
        return RasterPlan{std::move(meta), rasterBytes};
    }

    core::Result<void> checkDestination(const core::ImageMeta &meta, const pvd::PixelFormat format,
                                        const std::size_t destinationSize, const std::uint32_t pitchBytes)
    {
        if (format == pvd::PixelFormat::Bgra64 || format == pvd::PixelFormat::Rgba64 ||
            format == pvd::PixelFormat::Rgb48) {
            return std::unexpected(core::Error{core::ErrorCode::UnsupportedFeature,
                                               "SVG renders 8-bit samples; 16-bit output is unavailable"});
        }

        const auto bytesPerPixel = pvd::pixelLayout(format).bytesPerPixel;
        const auto expectedPitch = static_cast<std::uint64_t>(meta.width) * bytesPerPixel;
        if (pitchBytes != expectedPitch) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller pitch is not one tightly packed row"});
        }
        const auto expectedSize = expectedPitch * meta.height;
        if (destinationSize < expectedSize) {
            return std::unexpected(core::Error{core::ErrorCode::Internal, "caller buffer is smaller than the image"});
        }
        return {};
    }

    void convertPremultiplied(const std::span<const std::byte> premultiplied, const std::uint32_t width,
                              const std::uint32_t height, const pvd::PixelFormat format,
                              const std::span<std::byte> destination, const std::uint32_t pitchBytes) noexcept
    {
        const auto layout = pvd::pixelLayout(format);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t source = (static_cast<std::size_t>(y) * width + x) * kRgbaBytes;
                const std::size_t target =
                    static_cast<std::size_t>(y) * pitchBytes + static_cast<std::size_t>(x) * layout.bytesPerPixel;
                const auto alpha = std::to_integer<std::uint8_t>(premultiplied[source + 3U]);
                const auto straight = [alpha, premultiplied](const std::size_t channel) {
                    if (alpha == 0) {
                        return std::uint8_t{0};
                    }
                    const auto value =
                        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(premultiplied[channel])) * 255U +
                         alpha / 2U) /
                        alpha;
                    return static_cast<std::uint8_t>(value > 255U ? 255U : value);
                };

                const std::uint8_t red = straight(source);
                const std::uint8_t green = straight(source + 1U);
                const std::uint8_t blue = straight(source + 2U);
                if (layout.rgb) {
                    destination[target] = static_cast<std::byte>(red);
                    destination[target + 1U] = static_cast<std::byte>(green);
                    destination[target + 2U] = static_cast<std::byte>(blue);
                } else {
                    destination[target] = static_cast<std::byte>(blue);
                    destination[target + 1U] = static_cast<std::byte>(green);
                    destination[target + 2U] = static_cast<std::byte>(red);
                }
                if (layout.bytesPerPixel == 4U) {
                    destination[target + 3U] = static_cast<std::byte>(alpha);
                }
            }
        }
    }

} // namespace pvdkit::svg
