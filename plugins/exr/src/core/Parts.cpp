#include "core/Parts.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace pvdkit::exr
{

    core::Result<PartChoice> choosePart(const std::span<const PartInfo> parts)
    {
        const bool deepSkipped = std::ranges::any_of(parts, &PartInfo::deep);
        std::optional<PartChoice> first;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            const auto &part = parts[index];
            if (part.deep) {
                continue;
            }
            auto selection = selectChannels(part.channels, part.multiView);
            if (!selection) {
                continue;
            }
            if (part.view == "left") {
                return PartChoice{index, std::move(*selection), deepSkipped};
            }
            if (!first) {
                first = PartChoice{index, std::move(*selection), deepSkipped};
            }
        }
        if (first) {
            return std::move(*first);
        }
        const bool anyFlat = std::ranges::any_of(parts, [](const PartInfo &part) { return !part.deep; });
        if (deepSkipped && !anyFlat) {
            return std::unexpected(
                core::Error{core::ErrorCode::UnsupportedFeature, "only deep parts, which this plugin does not show"});
        }
        return std::unexpected(core::Error{core::ErrorCode::UnsupportedFeature, "no part carries colour channels"});
    }

} // namespace pvdkit::exr
