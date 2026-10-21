#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"

namespace pvdkit::svg
{

    /// Geometry and allocation size validated against the renderer and host policy. Only
    /// `planRaster` produces a plan, so a decoder never recomputes (or re-fails) these bounds.
    struct RasterPlan
    {
        core::ImageMeta meta;
        std::size_t rasterBytes = 0;
    };

    /// Applies the upstream `max(1, round)` rule to a positive finite intrinsic size, then the
    /// host policy, tiny-skia's `INT32_MAX / 4` row limit and the addressable byte count.
    [[nodiscard]] core::Result<RasterPlan> planRaster(float width, float height, const core::DecoderOptions &options);

    /// Rejects formats this renderer cannot emit and destination buffers that are not one tightly
    /// packed image.
    [[nodiscard]] core::Result<void> checkDestination(const core::ImageMeta &meta, pvd::PixelFormat format,
                                                      std::size_t destinationSize, std::uint32_t pitchBytes);

    /// Converts tiny-skia's premultiplied RGBA8 to the host's straight-alpha byte order. The caller
    /// must have validated the destination with `checkDestination`.
    void convertPremultiplied(std::span<const std::byte> premultiplied, std::uint32_t width, std::uint32_t height,
                              pvd::PixelFormat format, std::span<std::byte> destination,
                              std::uint32_t pitchBytes) noexcept;

} // namespace pvdkit::svg
