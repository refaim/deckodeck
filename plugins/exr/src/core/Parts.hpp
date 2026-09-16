#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/Channels.hpp"
#include "core/Error.hpp"

namespace pvdkit::exr
{

    /// What the adapter reads about one part before any pixel is touched.
    struct PartInfo
    {
        bool deep = false;                  ///< `deepscanline` / `deeptile` storage.
        std::string view;                   ///< The part's `view` attribute; empty when absent.
        std::vector<ChannelInfo> channels;  ///< The part's channel list in file order.
        std::vector<std::string> multiView; ///< The single-part `multiView` attribute; empty when absent.
    };

    /// The part the plugin shows and how.
    struct PartChoice
    {
        std::size_t index = 0;
        ChannelSelection selection;
        bool deepSkipped = false; ///< At least one deep part was passed over.
    };

    /// Picks the part to display (plugins/exr/DESIGN.md, "Part and view selection"): deep parts are
    /// skipped, a part must yield a channel selection, the `left` view is preferred among the
    /// candidates and otherwise the first candidate wins. `UnsupportedFeature` names why nothing is
    /// displayable (deep-only, or no colour channels).
    [[nodiscard]] core::Result<PartChoice> choosePart(std::span<const PartInfo> parts);

} // namespace pvdkit::exr
