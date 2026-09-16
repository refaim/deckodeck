#pragma once

#include <cstdint>
#include <optional>

#include "core/Error.hpp"
#include "core/IDecoder.hpp"

namespace pvdkit::exr
{

    /// An OpenEXR window: inclusive pixel bounds in the file's absolute coordinates.
    struct Box
    {
        std::int32_t xMin = 0;
        std::int32_t yMin = 0;
        std::int32_t xMax = 0;
        std::int32_t yMax = 0;

        [[nodiscard]] std::int64_t width() const noexcept
        {
            return static_cast<std::int64_t>(xMax) - xMin + 1;
        }
        [[nodiscard]] std::int64_t height() const noexcept
        {
            return static_cast<std::int64_t>(yMax) - yMin + 1;
        }
    };

    /// The picture the host receives: the display window's size and the part of the data window
    /// that falls inside it (absolute coordinates), or no overlap at all.
    struct Layout
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::int32_t originX = 0; ///< The display window's minimum, so overlap coordinates map to rows and columns.
        std::int32_t originY = 0;
        std::optional<Box> overlap;
    };

    /// Validates the two windows and computes the layout (plugins/exr/DESIGN.md, "Windows"):
    /// inverted or empty windows are `ParseFailed`; the display window's sides and area, and the
    /// data window's sides (they size the per-chunk scratch), are checked against
    /// `DecoderOptions::maxDimension` / `maxPixels` as `TooLarge` before any allocation.
    [[nodiscard]] core::Result<Layout> layout(const Box &display, const Box &data, const core::DecoderOptions &options);

    /// `TooLarge` when the data window's area exceeds `options.maxPixels`. `layout` bounds only the
    /// data window's sides, because RGB and tiled parts are read one chunk at a time and cropped to
    /// the overlap; a luminance/chroma part is reconstructed over its whole data window
    /// (plugins/exr/DESIGN.md, "Luminance/chroma files"), so the decoder bounds its area with this
    /// before allocating anything.
    [[nodiscard]] core::Result<void> checkDataArea(const Box &data, const core::DecoderOptions &options);

} // namespace pvdkit::exr
