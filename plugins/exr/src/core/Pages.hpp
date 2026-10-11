#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/Parts.hpp"

namespace pvdkit::exr
{
    /// One view/layer's channels, normalized to bare component names by the adapter.
    struct ChannelGroup
    {
        std::string view;
        std::string layer;
        std::vector<ChannelInfo> channels;
        std::vector<std::string> sourceNames;
    };

    /// Splits the library-resolved channels by view and colour layer in stable name order.
    [[nodiscard]] std::vector<ChannelGroup> channelGroups(const PartInfo &part);

    struct PageChoice
    {
        PartChoice part;
        std::string view;
    };

    /// Keeps the previous preferred selection first, then file part and sorted group order.
    [[nodiscard]] core::Result<std::vector<PageChoice>> pageChoices(std::span<const PartInfo> parts,
                                                                    std::span<const std::vector<ChannelGroup>> groups);
} // namespace pvdkit::exr
