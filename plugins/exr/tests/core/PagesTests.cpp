#include "core/Pages.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace
{
    pvdkit::exr::PartInfo pagePart(const std::vector<std::string> &names, const std::string &view = {})
    {
        pvdkit::exr::PartInfo part;
        part.view = view;
        for (const auto &name : names) {
            part.channels.push_back({name, pvdkit::exr::PixelType::Half, 1, 1, view, name});
        }
        return part;
    }

    auto catalogue(const std::vector<pvdkit::exr::PartInfo> &parts)
    {
        std::vector<std::vector<pvdkit::exr::ChannelGroup>> groups;
        groups.reserve(parts.size());
        for (const auto &part : parts) {
            groups.push_back(pvdkit::exr::channelGroups(part));
        }
        return pvdkit::exr::pageChoices(parts, groups);
    }
} // namespace

TEST_CASE("EXR page catalogue does not add utility-only parts beside colour pages")
{
    auto pages = catalogue({pagePart({"R", "G", "B"}), pagePart({"Z", "mask"})});
    REQUIRE(pages);
    REQUIRE(pages->size() == 1);
    CHECK(pages->front().part.index == 0);
    // The previous left-view choice is retained even when it was a grey fallback.
    pages = catalogue({pagePart({"R", "G", "B"}), pagePart({"Z"}, "left"), pagePart({"mask"})});
    REQUIRE(pages);
    REQUIRE(pages->size() == 2);
    CHECK(pages->front().part.index == 1);
    CHECK(pages->front().part.selection.kind == pvdkit::exr::ChannelKind::Single);
    CHECK(pages->back().part.index == 0);
}

TEST_CASE("EXR page catalogue ignores unsupported integer-only extra groups")
{
    auto part = pagePart({"R", "G", "B"});
    part.channels.push_back({"ids.value", pvdkit::exr::PixelType::Uint, 1, 1, {}, "ids.value"});
    const auto pages = catalogue({part});
    REQUIRE(pages);
    REQUIRE(pages->size() == 1);
    CHECK(pages->front().part.selection.kind == pvdkit::exr::ChannelKind::Rgb);
}
