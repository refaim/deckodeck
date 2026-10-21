#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace pvdkit::svg::tests
{

    struct FixtureExpectation
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
    };

    inline constexpr std::array kAccepted{
        FixtureExpectation{"alpha-composite.svg", 1, 1},
        FixtureExpectation{"alpha-ramp.svg", 3, 1},
        FixtureExpectation{"basic-2x2-colors.svg", 2, 2},
        FixtureExpectation{"clip.svg", 4, 2},
        FixtureExpectation{"filter-color-matrix.svg", 2, 2},
        FixtureExpectation{"gradient-linear.svg", 4, 1},
        FixtureExpectation{"image-data-uri.svg", 2, 2},
        FixtureExpectation{"image-relative.svg", 2, 2},
        FixtureExpectation{"mask.svg", 4, 2},
        FixtureExpectation{"text-cyrillic.svg", 160, 48},
        FixtureExpectation{"transform-rotate-90.svg", 4, 4},
        FixtureExpectation{"transform-translate-scale.svg", 4, 4},
        FixtureExpectation{"units-in.svg", 96, 48},
        FixtureExpectation{"valid.svgz", 2, 2},
        FixtureExpectation{"viewbox-only.svg", 6, 3},
        FixtureExpectation{"viewbox-scale.svg", 8, 4},
        FixtureExpectation{"xml-declaration-comments.svg", 3, 1},
    };

    // Obsolete components of the earlier fixture set stay on disk for authored-staging provenance.
    // The shared sequence driver discovers them as top-level inputs, so each one has an explicit
    // disposition and independent analytic size rather than falling through to nullopt.
    inline constexpr std::array kSupportAccepted{
        FixtureExpectation{"alpha_1x1.svg", 1, 1},  FixtureExpectation{"basic_2x2.svg", 2, 2},
        FixtureExpectation{"basic_2x2.svgz", 2, 2}, FixtureExpectation{"opaque_1x1.svg", 1, 1},
        FixtureExpectation{"resource.svg", 48, 48}, FixtureExpectation{"text_200x48.svg", 200, 48},
    };

    inline constexpr std::array<std::string_view, 9> kRejected{
        "assets/checker-2x2.png", "gzip_garbage.svgz", "invalid-utf8.svg",
        "malformed.svg",          "not_svg.txt",       "not-svg.svg",
        "resource.png",           "too_large.svg",     "truncated.svgz",
    };

} // namespace pvdkit::svg::tests
