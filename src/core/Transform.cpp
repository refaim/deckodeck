#include "core/Transform.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace pvdkit::core::Transform
{
    namespace
    {

        CropRect fullImage(const PixelView &view) noexcept
        {
            return CropRect{0, 0, view.width, view.height};
        }

        // One pass for clap -> irot -> imir over `rect`. Precondition: `rect` lies
        // inside `view` (it is either `fullImage(view)` or the result of
        // `validatedCrop`), which `apply` guarantees; the kernel does not re-check. The
        // final buffer is allocated once and every output pixel is walked back to its
        // source pixel, so there is no intermediate buffer. Rotation and mirroring keep
        // the pixel count, hence the single `PixelBuffer::create` on the cropped size
        // is the maxPixels / overflow check for the whole chain (and the only way this
        // function can fail).
        Result<PixelBuffer> transform(const PixelView &view, const CropRect &rect, const std::uint8_t angle,
                                      const std::optional<MirrorAxis> axis, const std::uint64_t maxPixels)
        {
            // 'irot' (HEIF ISO/IEC 23008-12:2017 6.5.10), installed avif.h:521-524:
            // "angle * 90 specifies the angle (in anti-clockwise direction) in units of
            // degrees", legal values [0-3]. An odd number of quarter turns swaps the
            // dimensions.
            const auto quarterTurns = static_cast<std::uint8_t>(angle % 4);
            const bool swapsDimensions = (quarterTurns % 2) != 0;
            const auto outputWidth = swapsDimensions ? rect.height : rect.width;
            const auto outputHeight = swapsDimensions ? rect.width : rect.height;
            auto created = PixelBuffer::create(outputWidth, outputHeight, view.bytesPerPixel, maxPixels);
            if (!created) {
                return std::unexpected(created.error());
            }
            auto &output = *created;

            for (std::uint32_t y = 0; y < outputHeight; ++y) {
                for (std::uint32_t x = 0; x < outputWidth; ++x) {
                    // 'imir' (HEIF ISO/IEC 23008-12:2022 6.5.12), installed avif.h:529-534:
                    // "'axis' specifies how the mirroring is performed: 0 indicates that
                    // the top and bottom parts of the image are exchanged; 1 specifies that
                    // the left and right parts are exchanged." The adapter maps axis 0 to
                    // MirrorAxis::TopBottom (rows reversed) and axis 1 to
                    // MirrorAxis::LeftRight (columns reversed). imir is applied last, so it
                    // is undone first: (x, y) -> position in the rotated image.
                    const auto rotatedX = axis == MirrorAxis::LeftRight ? outputWidth - 1 - x : x;
                    const auto rotatedY = axis == MirrorAxis::TopBottom ? outputHeight - 1 - y : y;

                    // Undo the anti-clockwise rotation by quarterTurns * 90 degrees
                    // (avif.h:523): rotated (rotatedX, rotatedY) -> position in the cropped
                    // image of rect.width x rect.height.
                    //   1 (90 deg):  the cropped image's right column became the top row,
                    //                so rotated (x, y) = cropped (width - 1 - y, x).
                    //   2 (180 deg): both axes reversed.
                    //   3 (270 deg): the cropped image's left column became the top row,
                    //                so rotated (x, y) = cropped (y, height - 1 - x).
                    std::uint32_t croppedX = 0;
                    std::uint32_t croppedY = 0;
                    switch (quarterTurns) {
                    case 0:
                        croppedX = rotatedX;
                        croppedY = rotatedY;
                        break;
                    case 1:
                        croppedX = rect.width - 1 - rotatedY;
                        croppedY = rotatedX;
                        break;
                    case 2:
                        croppedX = rect.width - 1 - rotatedX;
                        croppedY = rect.height - 1 - rotatedY;
                        break;
                    default:
                        croppedX = rotatedY;
                        croppedY = rect.height - 1 - rotatedX;
                        break;
                    }

                    // Undo 'clap': the cropped image is the source offset by the rectangle
                    // origin.
                    const auto sourceOffset = (static_cast<std::size_t>(rect.y) + croppedY) * view.pitchBytes +
                                              (static_cast<std::size_t>(rect.x) + croppedX) * view.bytesPerPixel;
                    const auto destinationOffset = static_cast<std::size_t>(y) * output.pitchBytes() +
                                                   static_cast<std::size_t>(x) * view.bytesPerPixel;
                    std::copy_n(view.pixels.subspan(sourceOffset).begin(), view.bytesPerPixel,
                                output.bytes().subspan(destinationOffset).begin());
                }
            }
            return created;
        }

    } // namespace

    Result<CropRect> validatedCrop(const CropRect &rect, const std::uint32_t imageWidth,
                                   const std::uint32_t imageHeight)
    {
        const auto right = static_cast<std::uint64_t>(rect.x) + rect.width;
        const auto bottom = static_cast<std::uint64_t>(rect.y) + rect.height;
        // Bitwise `&` on purpose: every operand is a side-effect-free bool, and one
        // non-short-circuiting expression is one branch region instead of four
        // (100% branch coverage would otherwise need a test per rejection order).
        const bool valid = (rect.width != 0) & (rect.height != 0) & (right <= imageWidth) & (bottom <= imageHeight);
        if (!valid) {
            return std::unexpected(Error{ErrorCode::InvalidTransform, "crop rectangle is outside the image"});
        }
        return rect;
    }

    std::pair<std::uint32_t, std::uint32_t> displaySize(const ImageMeta &meta) noexcept
    {
        auto width = meta.width;
        auto height = meta.height;
        if (meta.transforms.clap) {
            width = meta.transforms.clap->width;
            height = meta.transforms.clap->height;
        }
        if ((meta.transforms.irotAngle % 2) != 0) {
            std::swap(width, height);
        }
        return {width, height};
    }

    bool hasTransforms(const Transforms &transforms) noexcept
    {
        return transforms.clap.has_value() || transforms.irotAngle != 0 || transforms.imir.has_value();
    }

    Result<PixelBuffer> apply(const Transforms &transforms, const PixelView &view, const std::uint64_t maxPixels)
    {
        // Order: the AVIF spec's "Transformative properties" clause (ISO/IEC
        // 23000-22:2024 7.3.6.7, the clause the installed avif.h:495 cites for
        // clean aperture) requires clap -> irot -> imir. avif.h:815-819
        // (transformFlags) says these boxes "do not impact/adjust the actual pixel
        // buffers used (images won't be pre-cropped or mirrored upon decode)", so
        // core applies them.
        //
        // With 'clap' absent the rectangle is the full image, so `apply` always
        // yields a buffer: with no transform at all it is a tightly packed identity
        // copy, still subject to maxPixels.
        auto rect = fullImage(view);
        if (transforms.clap) {
            const auto valid = validatedCrop(*transforms.clap, view.width, view.height);
            if (!valid) {
                return std::unexpected(valid.error());
            }
            rect = *valid;
        }
        return transform(view, rect, transforms.irotAngle, transforms.imir, maxPixels);
    }

} // namespace pvdkit::core::Transform
