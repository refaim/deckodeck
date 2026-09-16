#include "core/Channels.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace pvdkit::exr
{
    namespace
    {

        using Channels = std::span<const ChannelInfo>;

        // A channel of the given name with the sampling the layout expects; UINT is never colour.
        std::optional<ChannelInfo> usable(const Channels channels, const std::string &name, const std::int32_t sampling)
        {
            const auto found = std::ranges::find(channels, name, &ChannelInfo::name);
            if (found == channels.end() || found->type == PixelType::Uint || found->xSampling != sampling ||
                found->ySampling != sampling) {
                return std::nullopt;
            }
            return *found;
        }

        std::string alphaOf(const Channels channels, const std::string &prefix)
        {
            return usable(channels, prefix + "A", 1) ? prefix + "A" : std::string{};
        }

        std::optional<ChannelSelection> rgb(const Channels channels, const std::string &layer)
        {
            const std::string prefix = layer.empty() ? std::string{} : layer + ".";
            const auto red = usable(channels, prefix + "R", 1);
            if (!red || !usable(channels, prefix + "G", 1) || !usable(channels, prefix + "B", 1)) {
                return std::nullopt;
            }
            return ChannelSelection{ChannelKind::Rgb,
                                    layer,
                                    {prefix + "R", prefix + "G", prefix + "B"},
                                    alphaOf(channels, prefix),
                                    red->type};
        }

        std::optional<ChannelSelection> luminance(const Channels channels)
        {
            const auto luma = usable(channels, "Y", 1);
            if (!luma) {
                return std::nullopt;
            }
            if (usable(channels, "RY", 2) && usable(channels, "BY", 2)) {
                return ChannelSelection{
                    ChannelKind::LuminanceChroma, {}, {"Y", "RY", "BY"}, alphaOf(channels, ""), luma->type};
            }
            return ChannelSelection{ChannelKind::Luminance, {}, {"Y", "", ""}, alphaOf(channels, ""), luma->type};
        }

        std::optional<ChannelSelection> firstLayer(const Channels channels)
        {
            std::vector<std::string> layers;
            for (const auto &channel : channels) {
                const auto dot = channel.name.rfind('.');
                if (dot != std::string::npos) {
                    layers.push_back(channel.name.substr(0, dot));
                }
            }
            std::ranges::sort(layers);
            const auto duplicates = std::ranges::unique(layers);
            layers.erase(duplicates.begin(), duplicates.end());
            for (const auto &layer : layers) {
                if (auto selection = rgb(channels, layer)) {
                    return selection;
                }
            }
            return std::nullopt;
        }

        std::optional<ChannelSelection> firstChannel(const Channels channels)
        {
            for (const auto &channel : channels) {
                if (usable(channels, channel.name, 1)) {
                    return ChannelSelection{ChannelKind::Single, {}, {channel.name, "", ""}, {}, channel.type};
                }
            }
            return std::nullopt;
        }

    } // namespace

    std::optional<ChannelSelection> selectChannels(const std::span<const ChannelInfo> channels,
                                                   const std::span<const std::string> multiView)
    {
        if (!multiView.empty() && multiView.front() != "left") {
            if (auto left = rgb(channels, "left")) {
                return left;
            }
        }
        if (auto topLevel = rgb(channels, {})) {
            return topLevel;
        }
        if (auto luma = luminance(channels)) {
            return luma;
        }
        if (auto layer = firstLayer(channels)) {
            return layer;
        }
        return firstChannel(channels);
    }

} // namespace pvdkit::exr
