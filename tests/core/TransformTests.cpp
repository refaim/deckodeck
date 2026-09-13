#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "core/Transform.hpp"

namespace pvdkit::core
{
    namespace
    {

        struct TestImage
        {
            std::vector<std::byte> pixels;
            std::uint32_t width;
            std::uint32_t height;
            std::uint32_t bytesPerPixel;
            std::uint32_t pitch;

            [[nodiscard]] PixelView view() const
            {
                return PixelView{pixels, width, height, bytesPerPixel, pitch};
            }
        };

        TestImage image(const std::uint32_t width, const std::uint32_t height, const std::uint32_t bytesPerPixel,
                        const std::initializer_list<unsigned> values, const std::uint32_t padding = 0)
        {
            const auto pitch = width * bytesPerPixel + padding;
            TestImage result{std::vector<std::byte>(static_cast<std::size_t>(pitch) * height), width, height,
                             bytesPerPixel, pitch};
            auto value = values.begin();
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    for (std::uint32_t channel = 0; channel < bytesPerPixel; ++channel) {
                        const auto offset =
                            static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * bytesPerPixel + channel;
                        result.pixels[offset] = std::byte{static_cast<unsigned char>(*value + channel * 16)};
                    }
                    ++value;
                }
            }
            return result;
        }

        std::vector<unsigned> pixelIds(const PixelView view)
        {
            std::vector<unsigned> result;
            for (std::uint32_t y = 0; y < view.height; ++y) {
                for (std::uint32_t x = 0; x < view.width; ++x) {
                    result.push_back(
                        std::to_integer<unsigned>(view.pixels[static_cast<std::size_t>(y) * view.pitchBytes +
                                                              static_cast<std::size_t>(x) * view.bytesPerPixel]));
                }
            }
            return result;
        }

        void checkPixels(const PixelBuffer &actual, const std::initializer_list<unsigned> expected,
                         const std::uint32_t width, const std::uint32_t height, const std::uint32_t bytesPerPixel)
        {
            CHECK(actual.width() == width);
            CHECK(actual.height() == height);
            CHECK(actual.bytesPerPixel() == bytesPerPixel);
            CHECK(actual.pitchBytes() == width * bytesPerPixel);
            const auto ids = pixelIds(actual.view());
            CHECK(ids == std::vector<unsigned>(expected));

            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    for (std::uint32_t channel = 0; channel < bytesPerPixel; ++channel) {
                        const auto offset = static_cast<std::size_t>(y) * actual.pitchBytes() +
                                            static_cast<std::size_t>(x) * bytesPerPixel + channel;
                        CHECK(std::to_integer<unsigned>(actual.bytes()[offset]) ==
                              ids[static_cast<std::size_t>(y) * width + x] + channel * 16);
                    }
                }
            }
        }

        ImageMeta metaWith(const std::uint32_t width, const std::uint32_t height, const Transforms &transforms)
        {
            return ImageMeta{
                width, height, 8,    ChromaFormat::Yuv420, false, false, Cicp{1, 13, 6, false}, 1, false, transforms,
                false, false,  false};
        }

        TEST_CASE("validatedCrop accepts each image corner")
        {
            for (const auto &[rect, expected] : std::array{std::pair{CropRect{0, 0, 2, 2}, CropRect{0, 0, 2, 2}},
                                                           std::pair{CropRect{1, 0, 2, 2}, CropRect{1, 0, 2, 2}},
                                                           std::pair{CropRect{0, 1, 2, 2}, CropRect{0, 1, 2, 2}},
                                                           std::pair{CropRect{1, 1, 2, 2}, CropRect{1, 1, 2, 2}}}) {
                const auto validated = Transform::validatedCrop(rect, 3, 3);
                REQUIRE(validated.has_value());
                CHECK(validated->x == expected.x);
                CHECK(validated->y == expected.y);
                CHECK(validated->width == expected.width);
                CHECK(validated->height == expected.height);
            }
        }

        TEST_CASE("validatedCrop rejects empty and outside rectangles")
        {
            for (const auto rect : {CropRect{0, 0, 0, 1}, CropRect{0, 0, 1, 0}, CropRect{3, 0, 1, 1},
                                    CropRect{0, 3, 1, 1}, CropRect{2, 0, 2, 1}, CropRect{0, 2, 1, 2},
                                    CropRect{std::numeric_limits<std::uint32_t>::max(), 0, 2, 1}}) {
                const auto validated = Transform::validatedCrop(rect, 3, 3);
                REQUIRE_FALSE(validated.has_value());
                CHECK(validated.error().code == ErrorCode::InvalidTransform);
            }
        }

        // Single-transform helpers: `apply` with exactly one property set, so each
        // operation is pinned on its own with the literal expectation matrices below.
        PixelBuffer cropOnly(const PixelView view, const CropRect &rect)
        {
            auto result =
                Transform::apply(Transforms{rect, 0, std::nullopt}, view, std::numeric_limits<std::uint64_t>::max());
            REQUIRE(result.has_value());
            return std::move(*result);
        }

        PixelBuffer rotateOnly(const PixelView view, const std::uint8_t angle)
        {
            auto result = Transform::apply(Transforms{std::nullopt, angle, std::nullopt}, view,
                                           std::numeric_limits<std::uint64_t>::max());
            REQUIRE(result.has_value());
            return std::move(*result);
        }

        PixelBuffer mirrorOnly(const PixelView view, const MirrorAxis axis)
        {
            auto result =
                Transform::apply(Transforms{std::nullopt, 0, axis}, view, std::numeric_limits<std::uint64_t>::max());
            REQUIRE(result.has_value());
            return std::move(*result);
        }

        TEST_CASE("crop copies all channels from padded rows at each corner")
        {
            const auto source = image(3, 3, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9}, 2);

            checkPixels(cropOnly(source.view(), CropRect{0, 0, 2, 2}), {1, 2, 4, 5}, 2, 2, 3);
            checkPixels(cropOnly(source.view(), CropRect{1, 0, 2, 2}), {2, 3, 5, 6}, 2, 2, 3);
            checkPixels(cropOnly(source.view(), CropRect{0, 1, 2, 2}), {4, 5, 7, 8}, 2, 2, 3);
            checkPixels(cropOnly(source.view(), CropRect{1, 1, 2, 2}), {5, 6, 8, 9}, 2, 2, 3);
        }

        TEST_CASE("rotate applies every anti-clockwise quarter turn to a rectangular "
                  "BGR image")
        {
            const auto source = image(2, 3, 3, {1, 2, 3, 4, 5, 6}, 1);

            checkPixels(rotateOnly(source.view(), 0), {1, 2, 3, 4, 5, 6}, 2, 3, 3);
            checkPixels(rotateOnly(source.view(), 1), {2, 4, 6, 1, 3, 5}, 3, 2, 3);
            checkPixels(rotateOnly(source.view(), 2), {6, 5, 4, 3, 2, 1}, 2, 3, 3);
            checkPixels(rotateOnly(source.view(), 3), {5, 3, 1, 6, 4, 2}, 3, 2, 3);
        }

        TEST_CASE("mirror applies both axes to 2x3 and 3x2 BGRA images")
        {
            const auto tall = image(2, 3, 4, {1, 2, 3, 4, 5, 6}, 3);
            checkPixels(mirrorOnly(tall.view(), MirrorAxis::TopBottom), {5, 6, 3, 4, 1, 2}, 2, 3, 4);
            checkPixels(mirrorOnly(tall.view(), MirrorAxis::LeftRight), {2, 1, 4, 3, 6, 5}, 2, 3, 4);

            const auto wide = image(3, 2, 4, {1, 2, 3, 4, 5, 6}, 1);
            checkPixels(mirrorOnly(wide.view(), MirrorAxis::TopBottom), {4, 5, 6, 1, 2, 3}, 3, 2, 4);
            checkPixels(mirrorOnly(wide.view(), MirrorAxis::LeftRight), {3, 2, 1, 6, 5, 4}, 3, 2, 4);
        }

        TEST_CASE("displaySize crops before swapping odd rotations")
        {
            Transforms transforms;
            CHECK(Transform::displaySize(metaWith(5, 3, transforms)) == std::pair{5U, 3U});

            transforms.clap = CropRect{1, 0, 3, 2};
            CHECK(Transform::displaySize(metaWith(5, 3, transforms)) == std::pair{3U, 2U});

            transforms.irotAngle = 1;
            CHECK(Transform::displaySize(metaWith(5, 3, transforms)) == std::pair{2U, 3U});

            transforms.irotAngle = 2;
            CHECK(Transform::displaySize(metaWith(5, 3, transforms)) == std::pair{3U, 2U});

            transforms.irotAngle = 3;
            transforms.imir = MirrorAxis::TopBottom;
            CHECK(Transform::displaySize(metaWith(5, 3, transforms)) == std::pair{2U, 3U});
        }

        TEST_CASE("hasTransforms recognizes each transformative property")
        {
            CHECK_FALSE(Transform::hasTransforms(Transforms{}));
            CHECK(Transform::hasTransforms(Transforms{CropRect{0, 0, 1, 1}, 0, std::nullopt}));
            CHECK(Transform::hasTransforms(Transforms{std::nullopt, 1, std::nullopt}));
            CHECK(Transform::hasTransforms(Transforms{std::nullopt, 0, MirrorAxis::LeftRight}));
            CHECK(Transform::hasTransforms(Transforms{CropRect{0, 0, 1, 1}, 1, MirrorAxis::TopBottom}));
        }

        TEST_CASE("apply performs clap then irot then imir")
        {
            const auto source = image(3, 2, 4, {1, 2, 3, 4, 5, 6}, 2);
            const Transforms transforms{CropRect{1, 0, 2, 2}, 1, MirrorAxis::LeftRight};

            auto result = Transform::apply(transforms, source.view(), 4);

            REQUIRE(result.has_value());
            checkPixels(*result, {6, 3, 5, 2}, 2, 2, 4);
        }

        TEST_CASE("apply copies all eight bytes of BGRA64 pixels")
        {
            const auto source = image(2, 2, 8, {1, 2, 3, 4}, 3);

            auto result = Transform::apply(Transforms{std::nullopt, 1, MirrorAxis::LeftRight}, source.view(), 4);

            REQUIRE(result.has_value());
            checkPixels(*result, {4, 2, 3, 1}, 2, 2, 8);
        }

        TEST_CASE("apply supports each property independently")
        {
            const auto source = image(3, 2, 3, {1, 2, 3, 4, 5, 6});

            auto cropped = Transform::apply(Transforms{CropRect{1, 0, 2, 2}, 0, std::nullopt}, source.view(), 4);
            REQUIRE(cropped.has_value());
            checkPixels(*cropped, {2, 3, 5, 6}, 2, 2, 3);

            auto rotated = Transform::apply(Transforms{std::nullopt, 1, std::nullopt}, source.view(), 6);
            REQUIRE(rotated.has_value());
            checkPixels(*rotated, {3, 6, 2, 5, 1, 4}, 2, 3, 3);

            auto mirrored = Transform::apply(Transforms{std::nullopt, 0, MirrorAxis::TopBottom}, source.view(), 6);
            REQUIRE(mirrored.has_value());
            checkPixels(*mirrored, {4, 5, 6, 1, 2, 3}, 3, 2, 3);
        }

        TEST_CASE("apply without transforms returns an identity copy")
        {
            const auto source = image(3, 2, 3, {1, 2, 3, 4, 5, 6}, 2);

            auto identity = Transform::apply(Transforms{}, source.view(), 6);

            REQUIRE(identity.has_value());
            checkPixels(*identity, {1, 2, 3, 4, 5, 6}, 3, 2, 3);
            CHECK(identity->bytes().data() != source.pixels.data());
        }

        TEST_CASE("apply without transforms enforces the output pixel limit")
        {
            const auto source = image(2, 3, 3, {1, 2, 3, 4, 5, 6});

            const auto identity = Transform::apply(Transforms{}, source.view(), 5);

            REQUIRE_FALSE(identity.has_value());
            CHECK(identity.error().code == ErrorCode::TooLarge);
        }

        TEST_CASE("apply propagates invalid crops and output size limits")
        {
            const auto source = image(2, 3, 3, {1, 2, 3, 4, 5, 6});

            const auto invalid = Transform::apply(Transforms{CropRect{1, 2, 2, 2}, 0, std::nullopt}, source.view(), 6);
            REQUIRE_FALSE(invalid.has_value());
            CHECK(invalid.error().code == ErrorCode::InvalidTransform);

            const auto tooLarge = Transform::apply(Transforms{std::nullopt, 1, std::nullopt}, source.view(), 5);
            REQUIRE_FALSE(tooLarge.has_value());
            CHECK(tooLarge.error().code == ErrorCode::TooLarge);

            const auto croppedTooLarge =
                Transform::apply(Transforms{CropRect{0, 0, 2, 3}, 0, std::nullopt}, source.view(), 5);
            REQUIRE_FALSE(croppedTooLarge.has_value());
            CHECK(croppedTooLarge.error().code == ErrorCode::TooLarge);

            const auto mirroredTooLarge =
                Transform::apply(Transforms{std::nullopt, 0, MirrorAxis::LeftRight}, source.view(), 5);
            REQUIRE_FALSE(mirroredTooLarge.has_value());
            CHECK(mirroredTooLarge.error().code == ErrorCode::TooLarge);
        }

    } // namespace
} // namespace pvdkit::core
