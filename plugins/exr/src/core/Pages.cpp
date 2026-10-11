#include "core/Pages.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace pvdkit::exr
{
    namespace
    {
        ChannelSelection sourceSelection(ChannelSelection selection, const ChannelGroup &group)
        {
            const auto source = [&](const std::string &name) {
                const auto found = std::ranges::find(group.channels, name, &ChannelInfo::name);
                return group.sourceNames[static_cast<std::size_t>(found - group.channels.begin())];
            };
            for (auto &name : selection.colour) {
                if (!name.empty()) {
                    name = source(name);
                }
            }
            if (selection.hasAlpha()) {
                selection.alpha = source(selection.alpha);
            }
            selection.layer = group.layer;
            return selection;
        }

    } // namespace

    std::vector<ChannelGroup> channelGroups(const PartInfo &part)
    {
        std::map<std::pair<std::string, std::string>, ChannelGroup> grouped;
        for (const auto &channel : part.channels) {
            const auto &normalized = channel.nameWithoutView;
            const auto dot = normalized.rfind('.');
            const auto layer = dot == std::string::npos ? std::string{} : normalized.substr(0, dot);
            const auto component = dot == std::string::npos ? normalized : normalized.substr(dot + 1);
            auto &group = grouped[{channel.view, layer}];
            group.view = channel.view;
            group.layer = layer;
            auto entry = channel;
            entry.name = component;
            group.channels.push_back(std::move(entry));
            group.sourceNames.push_back(channel.name);
        }
        std::vector<ChannelGroup> groups;
        groups.reserve(grouped.size());
        for (auto &[key, group] : grouped) {
            groups.push_back(std::move(group));
        }
        return groups;
    }
    core::Result<std::vector<PageChoice>> pageChoices(const std::span<const PartInfo> parts,
                                                      const std::span<const std::vector<ChannelGroup>> groups)
    {
        return choosePart(parts).transform([&](PartChoice preferred) {
            std::vector<PageChoice> pages;
            pages.push_back({preferred, parts[preferred.index].view});
            for (std::size_t part = 0; part < parts.size(); ++part) {
                if (parts[part].deep) {
                    continue;
                }
                for (const auto &group : groups[part]) {
                    if (part == preferred.index &&
                        std::ranges::find(group.sourceNames, preferred.selection.colour[0]) !=
                            group.sourceNames.end()) {
                        // Preserve the complete legacy selection, even when normalization would
                        // choose another channel kind or attach an alias alpha in the same group.
                        pages.front().view = group.view;
                        continue;
                    }
                    auto selection = selectChannels(group.channels, {});
                    if (!selection || selection->kind == ChannelKind::Single) {
                        continue;
                    }
                    auto selected = sourceSelection(std::move(*selection), group);
                    pages.push_back({PartChoice{part, std::move(selected), preferred.deepSkipped}, group.view});
                }
            }
            return pages;
        });
    }
} // namespace pvdkit::exr
