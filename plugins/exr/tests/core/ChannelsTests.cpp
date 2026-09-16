#include <array>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/Channels.hpp"

namespace pvdkit::exr
{
    namespace
    {

        ChannelInfo half(const std::string &name)
        {
            return ChannelInfo{name, PixelType::Half, 1, 1};
        }

        ChannelInfo chroma(const std::string &name)
        {
            return ChannelInfo{name, PixelType::Half, 2, 2};
        }

        const std::vector<std::string> kNoViews{};

        TEST_CASE("top-level R G B are selected as RGB with an optional A of the same level")
        {
            const std::vector channels{half("B"), half("G"), half("R")};
            const auto selection = selectChannels(channels, kNoViews);
            REQUIRE(selection.has_value());
            CHECK(selection->kind == ChannelKind::Rgb);
            CHECK(selection->layer.empty());
            CHECK(selection->colour == std::array<std::string, 3>{"R", "G", "B"});
            CHECK(selection->alpha.empty());
            CHECK_FALSE(selection->hasAlpha());
            CHECK(selection->type == PixelType::Half);

            const std::vector withAlpha{half("A"), half("B"), half("G"), ChannelInfo{"R", PixelType::Float, 1, 1}};
            const auto rgba = selectChannels(withAlpha, kNoViews);
            REQUIRE(rgba.has_value());
            CHECK(rgba->alpha == "A");
            CHECK(rgba->hasAlpha());
            CHECK(rgba->type == PixelType::Float);
        }

        TEST_CASE("UINT and subsampled channels count as absent")
        {
            const std::vector uintRgb{ChannelInfo{"B", PixelType::Uint, 1, 1}, ChannelInfo{"G", PixelType::Uint, 1, 1},
                                      ChannelInfo{"R", PixelType::Uint, 1, 1}};
            CHECK_FALSE(selectChannels(uintRgb, kNoViews).has_value());

            const std::vector uintAlpha{ChannelInfo{"A", PixelType::Uint, 1, 1}, half("B"), half("G"), half("R")};
            const auto noAlpha = selectChannels(uintAlpha, kNoViews);
            REQUIRE(noAlpha.has_value());
            CHECK_FALSE(noAlpha->hasAlpha());

            const std::vector subsampledBlue{chroma("B"), half("G"), half("R")};
            const auto fallback = selectChannels(subsampledBlue, kNoViews);
            REQUIRE(fallback.has_value());
            CHECK(fallback->kind == ChannelKind::Single);
            CHECK(fallback->colour[0] == "G");

            const std::vector missingGreen{half("B"), half("R")};
            const auto blue = selectChannels(missingGreen, kNoViews);
            REQUIRE(blue.has_value());
            CHECK(blue->kind == ChannelKind::Single);
            CHECK(blue->colour[0] == "B");
        }

        TEST_CASE("Y with 2x2 RY and BY is luminance-chroma and Y alone is luminance")
        {
            const std::vector yca{chroma("BY"), chroma("RY"), half("Y")};
            const auto lumaChroma = selectChannels(yca, kNoViews);
            REQUIRE(lumaChroma.has_value());
            CHECK(lumaChroma->kind == ChannelKind::LuminanceChroma);
            CHECK(lumaChroma->colour == std::array<std::string, 3>{"Y", "RY", "BY"});
            CHECK_FALSE(lumaChroma->hasAlpha());

            const std::vector ycaAlpha{half("A"), chroma("BY"), chroma("RY"), half("Y")};
            const auto withAlpha = selectChannels(ycaAlpha, kNoViews);
            REQUIRE(withAlpha.has_value());
            CHECK(withAlpha->kind == ChannelKind::LuminanceChroma);
            CHECK(withAlpha->alpha == "A");

            const std::vector luminance{ChannelInfo{"Y", PixelType::Float, 1, 1}};
            const auto grey = selectChannels(luminance, kNoViews);
            REQUIRE(grey.has_value());
            CHECK(grey->kind == ChannelKind::Luminance);
            CHECK(grey->colour == std::array<std::string, 3>{"Y", "", ""});
            CHECK(grey->type == PixelType::Float);

            // Chroma at any other sampling is not the library's luminance-chroma layout.
            const std::vector oddChroma{half("BY"), half("RY"), half("Y")};
            const auto plainY = selectChannels(oddChroma, kNoViews);
            REQUIRE(plainY.has_value());
            CHECK(plainY->kind == ChannelKind::Luminance);
            const std::vector halfChroma{chroma("BY"), half("Y")};
            const auto missingRy = selectChannels(halfChroma, kNoViews);
            REQUIRE(missingRy.has_value());
            CHECK(missingRy->kind == ChannelKind::Luminance);
            const std::vector otherHalf{chroma("RY"), half("Y")};
            const auto missingBy = selectChannels(otherHalf, kNoViews);
            REQUIRE(missingBy.has_value());
            CHECK(missingBy->kind == ChannelKind::Luminance);
            CHECK(missingBy->colour == std::array<std::string, 3>{"Y", "", ""});
            const std::vector lineChroma{chroma("BY"), ChannelInfo{"RY", PixelType::Half, 2, 1}, half("Y")};
            const auto wrongRows = selectChannels(lineChroma, kNoViews);
            REQUIRE(wrongRows.has_value());
            CHECK(wrongRows->kind == ChannelKind::Luminance);
        }

        TEST_CASE("the first layer by name that carries R G B is used when the top level has none")
        {
            const std::vector layered{
                half("aaa.Z"),    half("beauty.A"), half("beauty.B"),
                half("beauty.G"), half("beauty.R"), half("depth.B"),
                half("depth.G"),  half("depth.R"),  ChannelInfo{"beauty.id", PixelType::Uint, 1, 1}};
            const auto selection = selectChannels(layered, kNoViews);
            REQUIRE(selection.has_value());
            CHECK(selection->kind == ChannelKind::Rgb);
            CHECK(selection->layer == "beauty");
            CHECK(selection->colour == std::array<std::string, 3>{"beauty.R", "beauty.G", "beauty.B"});
            CHECK(selection->alpha == "beauty.A");

            // Nested layer names keep everything before the last dot as the layer.
            const std::vector nested{half("light.key.B"), half("light.key.G"), half("light.key.R")};
            const auto deep = selectChannels(nested, kNoViews);
            REQUIRE(deep.has_value());
            CHECK(deep->layer == "light.key");

            // A layer missing one colour channel is skipped in favour of the next complete one.
            const std::vector partial{half("aaa.G"), half("aaa.R"), half("bbb.B"), half("bbb.G"), half("bbb.R")};
            const auto next = selectChannels(partial, kNoViews);
            REQUIRE(next.has_value());
            CHECK(next->layer == "bbb");
        }

        TEST_CASE("any remaining channel is shown as grey and an empty or unusable list selects nothing")
        {
            const std::vector single{ChannelInfo{"Z", PixelType::Float, 1, 1}};
            const auto grey = selectChannels(single, kNoViews);
            REQUIRE(grey.has_value());
            CHECK(grey->kind == ChannelKind::Single);
            CHECK(grey->colour == std::array<std::string, 3>{"Z", "", ""});
            CHECK_FALSE(grey->hasAlpha());
            CHECK(grey->type == PixelType::Float);

            const std::vector skipsUint{ChannelInfo{"id", PixelType::Uint, 1, 1}, chroma("odd"), half("zz")};
            const auto last = selectChannels(skipsUint, kNoViews);
            REQUIRE(last.has_value());
            CHECK(last->colour[0] == "zz");

            CHECK_FALSE(selectChannels(std::vector<ChannelInfo>{}, kNoViews).has_value());
            const std::vector onlyUint{ChannelInfo{"id", PixelType::Uint, 1, 1}};
            CHECK_FALSE(selectChannels(onlyUint, kNoViews).has_value());
            // A lone alpha is not colour either.
            const std::vector onlyAlpha{half("A")};
            const auto alpha = selectChannels(onlyAlpha, kNoViews);
            REQUIRE(alpha.has_value());
            CHECK(alpha->kind == ChannelKind::Single);
            CHECK(alpha->colour[0] == "A");
        }

        TEST_CASE("a multi-view file prefers the left view even when another view owns the bare channels")
        {
            const std::vector channels{half("B"),      half("G"),      half("R"),     half("left.A"),
                                       half("left.B"), half("left.G"), half("left.R")};
            const std::vector<std::string> rightFirst{"right", "left"};
            const auto left = selectChannels(channels, rightFirst);
            REQUIRE(left.has_value());
            CHECK(left->layer == "left");
            CHECK(left->alpha == "left.A");

            const std::vector<std::string> leftFirst{"left", "right"};
            const auto bare = selectChannels(channels, leftFirst);
            REQUIRE(bare.has_value());
            CHECK(bare->layer.empty());

            // Without a complete left layer the bare channels stay in charge.
            const std::vector noLeft{half("B"),       half("G"),       half("R"),
                                     half("right.B"), half("right.G"), half("right.R")};
            const auto fallback = selectChannels(noLeft, rightFirst);
            REQUIRE(fallback.has_value());
            CHECK(fallback->layer.empty());
        }

    } // namespace
} // namespace pvdkit::exr
