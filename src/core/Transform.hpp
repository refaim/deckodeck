#pragma once

#include <cstdint>
#include <utility>

#include "core/IDecoder.hpp"
#include "core/PixelBuffer.hpp"

namespace pvdkit::core::Transform {

[[nodiscard]] Result<CropRect> validatedCrop(const CropRect &rect,
                                             std::uint32_t imageWidth,
                                             std::uint32_t imageHeight);
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
displaySize(const ImageMeta &meta) noexcept;
[[nodiscard]] bool hasTransforms(const Transforms &transforms) noexcept;
[[nodiscard]] Result<PixelBuffer>
apply(const Transforms &transforms, PixelView view, std::uint64_t maxPixels);

} // namespace pvdkit::core::Transform
