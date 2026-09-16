#include "core/Windows.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace pvdkit::exr
{
    namespace
    {

        core::Result<void> checkShape(const Box &box, const std::string_view name)
        {
            if (box.xMax < box.xMin || box.yMax < box.yMin) {
                return std::unexpected(
                    core::Error{core::ErrorCode::ParseFailed, std::string{name} + " window is inverted or empty"});
            }
            return {};
        }

        core::Result<void> checkSides(const Box &box, const std::string_view name, const std::uint32_t maxDimension)
        {
            if (box.width() > maxDimension || box.height() > maxDimension) {
                return std::unexpected(
                    core::Error{core::ErrorCode::TooLarge, std::string{name} + " window exceeds the dimension limit"});
            }
            return {};
        }

    } // namespace

    core::Result<void> checkDataArea(const Box &data, const core::DecoderOptions &options)
    {
        const auto area = static_cast<std::uint64_t>(data.width()) * static_cast<std::uint64_t>(data.height());
        if (area > options.maxPixels) {
            return std::unexpected(core::Error{
                core::ErrorCode::TooLarge, "data window exceeds the pixel count limit for a luminance/chroma part"});
        }
        return {};
    }

    core::Result<Layout> layout(const Box &display, const Box &data, const core::DecoderOptions &options)
    {
        return checkShape(display, "display")
            .and_then([&] { return checkShape(data, "data"); })
            .and_then([&] { return checkSides(display, "display", options.maxDimension); })
            .and_then([&] { return checkSides(data, "data", options.maxDimension); })
            .and_then([&]() -> core::Result<Layout> {
                const auto width = static_cast<std::uint64_t>(display.width());
                const auto height = static_cast<std::uint64_t>(display.height());
                if (width * height > options.maxPixels) {
                    return std::unexpected(
                        core::Error{core::ErrorCode::TooLarge, "display window exceeds the pixel count limit"});
                }
                Layout result{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), display.xMin,
                              display.yMin, std::nullopt};
                const Box overlap{std::max(display.xMin, data.xMin), std::max(display.yMin, data.yMin),
                                  std::min(display.xMax, data.xMax), std::min(display.yMax, data.yMax)};
                if (overlap.xMax >= overlap.xMin && overlap.yMax >= overlap.yMin) {
                    result.overlap = overlap;
                }
                return result;
            });
    }

} // namespace pvdkit::exr
