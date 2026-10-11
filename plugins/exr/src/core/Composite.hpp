#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Windows.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::exr
{

    /// Writes RGB48, RGBA64 or legacy BGRA64 rows (little-endian 16-bit codes) of `pitchBytes`
    /// each into `dst`: the overlap's codes, already in the selected layout (three or four
    /// codes per pixel, row-major), at their display position, and black
    /// everywhere else: transparent black (alpha 0) when the picture has alpha, opaque black
    /// otherwise (plugins/exr/DESIGN.md, "Windows"). `dst` must hold `layout.height` rows.
    void composite(const Layout &layout, std::span<const std::uint16_t> overlap, bool hasAlpha,
                   std::span<std::byte> dst, std::uint32_t pitchBytes,
                   pvd::PixelFormat format = pvd::PixelFormat::Bgra64) noexcept;

} // namespace pvdkit::exr
