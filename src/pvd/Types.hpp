#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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

    /// Selects the byte layout returned to PictureView. BGRA formats carry straight alpha;
    /// Bgra64 stores each 16-bit channel sample in little-endian byte order.
    enum class PixelFormat : std::uint8_t
    {
        Bgr24,
        Bgra32,
        Bgra64
    };

    /// Views decoded pixel memory owned by the file session.
    struct DecodedPage
    {
        std::span<const std::byte> pixels;
        std::uint32_t bitsPerPixel = 0; ///< 24, 32 or 64.
        std::uint32_t pitchBytes = 0;   ///< Width times bytes per pixel, without padding.
        bool hasAlpha = false;          ///< The alpha channel carries information rather than being fully opaque.
    };

    /// Carries the host-provided inputs needed to open an image.
    struct OpenRequest
    {
        std::string_view utf8FileName;
        std::uint64_t fileSize = 0;
        std::span<const std::byte> head;
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
