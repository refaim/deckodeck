#include "core/Composite.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace pvdkit::exr
{
    namespace
    {

        constexpr std::size_t kBytesPerPixel = 8;

        // The codes of one pixel outside the data window: black, transparent or opaque.
        std::array<std::uint16_t, 4> fillPixel(const bool hasAlpha) noexcept
        {
            return {0, 0, 0, hasAlpha ? std::uint16_t{0} : std::uint16_t{65'535}};
        }

        void fillRow(const std::span<std::byte> row, const std::uint32_t firstColumn, const std::uint32_t columns,
                     const std::array<std::uint16_t, 4> &pixel) noexcept
        {
            for (std::uint32_t column = 0; column < columns; ++column) {
                std::memcpy(row.data() + static_cast<std::size_t>(firstColumn + column) * kBytesPerPixel, pixel.data(),
                            kBytesPerPixel);
            }
        }

    } // namespace

    void composite(const Layout &layout, const std::span<const std::uint16_t> overlap, const bool hasAlpha,
                   const std::span<std::byte> dst, const std::uint32_t pitchBytes) noexcept
    {
        const auto fill = fillPixel(hasAlpha);
        // Rows and columns of the overlap in display coordinates; an empty range when there is none.
        std::uint32_t firstRow = 0;
        std::uint32_t lastRow = 0;
        std::uint32_t firstColumn = 0;
        std::uint32_t columns = 0;
        if (layout.overlap) {
            firstRow = static_cast<std::uint32_t>(layout.overlap->yMin - layout.originY);
            lastRow = static_cast<std::uint32_t>(layout.overlap->yMax - layout.originY) + 1;
            firstColumn = static_cast<std::uint32_t>(layout.overlap->xMin - layout.originX);
            columns = static_cast<std::uint32_t>(layout.overlap->width());
        }
        const std::size_t overlapRowCodes = static_cast<std::size_t>(columns) * 4;
        for (std::uint32_t y = 0; y < layout.height; ++y) {
            const auto row = dst.subspan(static_cast<std::size_t>(y) * pitchBytes,
                                         static_cast<std::size_t>(layout.width) * kBytesPerPixel);
            if (y < firstRow || y >= lastRow) {
                fillRow(row, 0, layout.width, fill);
                continue;
            }
            fillRow(row, 0, firstColumn, fill);
            std::memcpy(row.data() + static_cast<std::size_t>(firstColumn) * kBytesPerPixel,
                        overlap.data() + static_cast<std::size_t>(y - firstRow) * overlapRowCodes, overlapRowCodes * 2);
            fillRow(row, firstColumn + columns, layout.width - firstColumn - columns, fill);
        }
    }

} // namespace pvdkit::exr
