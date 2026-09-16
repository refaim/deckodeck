#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace pvdkit::exr
{

    /// The sample type of one OpenEXR channel (the file's `pixelType`).
    enum class PixelType : std::uint8_t
    {
        Uint,
        Half,
        Float
    };

    /// One entry of a part's channel list as the adapter reads it from the header.
    struct ChannelInfo
    {
        std::string name;
        PixelType type = PixelType::Half;
        std::int32_t xSampling = 1;
        std::int32_t ySampling = 1;
    };

    /// How the selected channels turn into pixels.
    enum class ChannelKind : std::uint8_t
    {
        Rgb,             ///< `colour` holds R, G, B (top level or one layer); optional alpha.
        LuminanceChroma, ///< `colour` holds Y, RY, BY (RY/BY subsampled 2x2); optional alpha.
        Luminance,       ///< `colour[0]` is Y; optional alpha.
        Single           ///< `colour[0]` is some other channel shown as grey; no alpha.
    };

    /// The channels the decoder reads and the layout they form.
    struct ChannelSelection
    {
        ChannelKind kind = ChannelKind::Rgb;
        std::string layer; ///< The layer prefix (without its dot) of an `Rgb` selection; empty at top level.
        std::array<std::string, 3> colour; ///< Full channel names; unused slots are empty.
        std::string alpha;                 ///< The alpha channel's full name, empty when absent.
        PixelType type = PixelType::Half;  ///< The sample type of `colour[0]`.

        [[nodiscard]] bool hasAlpha() const noexcept
        {
            return !alpha.empty();
        }
    };

    /// Decides which channels of one part are displayed (plugins/exr/DESIGN.md, "Channels"):
    /// top-level R,G,B (+A); else Y with 2x2 RY,BY (+A); else Y alone (+A); else the first layer by
    /// name with R,G,B (+A); else the first usable channel as grey. UINT channels and channels with
    /// any sampling other than the layout expects count as absent. `multiView` is the single-part
    /// `multiView` attribute: when its first (bare-channel) view is not `left` and a complete `left`
    /// layer exists, that layer wins over the bare channels.
    [[nodiscard]] std::optional<ChannelSelection> selectChannels(std::span<const ChannelInfo> channels,
                                                                 std::span<const std::string> multiView);

} // namespace pvdkit::exr
