#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Windows.hpp"

namespace pvdkit::exr
{

    /// Writes the display window as tightly packed BGRA64 rows (little-endian 16-bit codes) of
    /// `pitchBytes` each into `dst`: the overlap's codes (`overlap` holds
    /// `layout.overlap` width x height x 4 codes, row-major) at their display position, and black
    /// everywhere else: transparent black (alpha 0) when the picture has alpha, opaque black
    /// otherwise (plugins/exr/DESIGN.md, "Windows"). `dst` must hold `layout.height` rows.
    void composite(const Layout &layout, std::span<const std::uint16_t> overlap, bool hasAlpha,
                   std::span<std::byte> dst, std::uint32_t pitchBytes) noexcept;

} // namespace pvdkit::exr
