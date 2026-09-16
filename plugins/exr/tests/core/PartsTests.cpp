#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/Channels.hpp"
#include "core/Error.hpp"
#include "core/Parts.hpp"

namespace pvdkit::exr
{
    namespace
    {

        PartInfo rgbPart(const std::string &view)
        {
            return PartInfo{false, view, {ChannelInfo{"B"}, ChannelInfo{"G"}, ChannelInfo{"R"}}, {}};
        }

        PartInfo deepPart()
        {
            return PartInfo{true, "", {ChannelInfo{"B"}, ChannelInfo{"G"}, ChannelInfo{"R"}}, {}};
        }

        PartInfo dataPart()
        {
            return PartInfo{false, "", {ChannelInfo{"id", PixelType::Uint}}, {}};
        }

        TEST_CASE("a single flat part with colour channels is chosen")
        {
            const std::vector parts{rgbPart("")};
            const auto choice = choosePart(parts);
            REQUIRE(choice.has_value());
            CHECK(choice->index == 0);
            CHECK(choice->selection.kind == ChannelKind::Rgb);
            CHECK_FALSE(choice->deepSkipped);
        }

        TEST_CASE("deep parts are skipped and the first displayable flat part wins")
        {
            const std::vector parts{deepPart(), dataPart(), rgbPart(""), rgbPart("")};
            const auto choice = choosePart(parts);
            REQUIRE(choice.has_value());
            CHECK(choice->index == 2);
            CHECK(choice->deepSkipped);
        }

        TEST_CASE("the left view is preferred among displayable parts whatever its position")
        {
            const std::vector parts{rgbPart("right"), rgbPart("left")};
            const auto choice = choosePart(parts);
            REQUIRE(choice.has_value());
            CHECK(choice->index == 1);

            // No left view: the first displayable part, even if it is the right view.
            const std::vector rightOnly{dataPart(), rgbPart("right"), rgbPart("centre")};
            const auto right = choosePart(rightOnly);
            REQUIRE(right.has_value());
            CHECK(right->index == 1);
        }

        TEST_CASE("files with nothing to show are refused with a reason")
        {
            const std::vector onlyDeep{deepPart(), deepPart()};
            const auto deep = choosePart(onlyDeep);
            REQUIRE_FALSE(deep.has_value());
            CHECK(deep.error().code == core::ErrorCode::UnsupportedFeature);
            CHECK(deep.error().detail.find("deep") != std::string::npos);

            const std::vector onlyData{dataPart(), deepPart()};
            const auto data = choosePart(onlyData);
            REQUIRE_FALSE(data.has_value());
            CHECK(data.error().code == core::ErrorCode::UnsupportedFeature);
            CHECK(data.error().detail.find("colour") != std::string::npos);

            const auto none = choosePart(std::vector<PartInfo>{});
            REQUIRE_FALSE(none.has_value());
            CHECK(none.error().code == core::ErrorCode::UnsupportedFeature);
        }

        TEST_CASE("the single-part multiView attribute reaches the channel selection")
        {
            PartInfo stereo{false,
                            "",
                            {ChannelInfo{"B"}, ChannelInfo{"G"}, ChannelInfo{"R"}, ChannelInfo{"left.B"},
                             ChannelInfo{"left.G"}, ChannelInfo{"left.R"}},
                            {"right", "left"}};
            const std::vector parts{stereo};
            const auto choice = choosePart(parts);
            REQUIRE(choice.has_value());
            CHECK(choice->selection.layer == "left");
        }

    } // namespace
} // namespace pvdkit::exr
