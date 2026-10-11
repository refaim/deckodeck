#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pvdkit::pvd
{

    /// Describes this decoder plugin to the PictureView host.
    struct PluginInfo
    {
        std::uint32_t priority = 0;
        std::string name, version, comments;
    };

    /// Describes an opened image to the PictureView host.
    struct ImageInfo
    {
        std::uint32_t pageCount = 0;
        bool animated = false;
        std::string formatName, compression, comments;
    };

    /// Describes one page or animation frame.
    struct PageInfo
    {
        std::uint32_t width, height, bitsPerPixel, frameTimeMs;
    };

    /// Selects the byte layout returned to PictureView. Alpha is straight; 16-bit samples
    /// are little-endian. The enum values are internal indices, not PVD nBPP codes.
    enum class PixelFormat : std::uint8_t
    {
        Bgr24,
        Bgra32,
        Bgra64,
        Rgb24,
        Rgba32,
        Rgba64,
        Rgb48
    };

    struct PixelLayout
    {
        std::uint32_t bytesPerPixel;
        std::uint32_t pvdCode;
        bool rgb;
    };

    /// Storage and wire metadata for a valid PixelFormat. RGB wire codes are not bit counts.
    [[nodiscard]] constexpr PixelLayout pixelLayout(const PixelFormat format) noexcept
    {
        constexpr std::array layouts{PixelLayout{3, 24, false}, PixelLayout{4, 32, false}, PixelLayout{8, 64, false},
                                     PixelLayout{3, 25, true},  PixelLayout{4, 33, true},  PixelLayout{8, 65, true},
                                     PixelLayout{6, 49, true}};
        return layouts[static_cast<std::size_t>(format)];
    }

    /// Views decoded pixel memory owned by the file session.
    struct DecodedPage
    {
        std::span<const std::byte> pixels;
        std::uint32_t bitsPerPixel = 0;   ///< PVD nBPP format code: 24/32/64 or 25/33/49/65; not a storage size.
        std::uint32_t pitchBytes = 0;     ///< Width times bytes per pixel, without padding.
        bool hasAlpha = false;            ///< The alpha channel carries information rather than being fully opaque.
        std::uint8_t hostOrientation = 0; ///< PictureView orientation code, 0..7.
    };

    /// Carries the host-provided inputs needed to open an image.
    struct OpenRequest
    {
        std::string_view utf8FileName;
        std::uint64_t fileSize = 0;
        std::span<const std::byte> head;
        std::optional<std::wstring_view> wideFileName{};
        /// ABI output policy: the SDK v1.1 wide entry requests native RGB; legacy callers do not.
        bool nativeRgb = false;
    };

    /// Wraps the optional PictureView progress callback.
    class Progress
    {
      public:
        /// Defines the C++ progress callback signature.
        using Fn = std::function<bool(std::uint32_t step, std::uint32_t steps)>;

        Progress() = default;
        explicit Progress(Fn fn);
        [[nodiscard]] bool report(std::uint32_t step, std::uint32_t steps) const;

      private:
        Fn fn_;
    };

} // namespace pvdkit::pvd
