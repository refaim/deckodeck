#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace pvdkit::exr::tests
{

    struct FixtureExpectation
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t pageBpp; // informational: sample bits (16 half / 32 float) x channels (3, 4 with alpha)
        bool alpha;
    };

    // Every fixture of plugins/exr/fixtures/SOURCES.md the plugin accepts; sizes are the display
    // windows. Every page is delivered as BGRA64 (the colour path), whatever the source.
    inline constexpr std::array kAccepted{
        FixtureExpectation{"rgb_half_zip.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_none.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_rle.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_zips.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_piz.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_pxr24.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_b44.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_b44a.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_dwaa.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_dwab.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_htj2k256.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_half_htj2k32.exr", 16, 8, 48, false},
        FixtureExpectation{"rgba_premultiplied.exr", 16, 8, 64, true},
        FixtureExpectation{"y_float_zips.exr", 16, 8, 96, false},
        FixtureExpectation{"layer_rgb.exr", 16, 8, 64, true},
        FixtureExpectation{"rgb_float_mixed.exr", 16, 8, 128, true},
        FixtureExpectation{"data_inside_display.exr", 16, 12, 64, true},
        FixtureExpectation{"data_larger_than_display.exr", 8, 6, 48, false},
        FixtureExpectation{"data_offset_partial.exr", 8, 8, 48, false},
        FixtureExpectation{"data_disjoint.exr", 8, 8, 64, true},
        FixtureExpectation{"decreasing_y.exr", 16, 8, 48, false},
        FixtureExpectation{"white_luminance_250.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_ap0.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_ap1.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_rec2020.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_p3d65.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_custom.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_xyz.exr", 16, 8, 48, false},
        FixtureExpectation{"rgb_whitey0.exr", 16, 8, 48, false},
        FixtureExpectation{"interop_lin_ap1.exr", 16, 8, 48, false},
        FixtureExpectation{"interop_unknown.exr", 16, 8, 48, false},
        FixtureExpectation{"peak_above_10000.exr", 16, 8, 48, false},
        FixtureExpectation{"nan_inf_negative.exr", 16, 8, 96, false},
        FixtureExpectation{"multipart_views.exr", 8, 8, 48, false},
        FixtureExpectation{"multipart_deep_flat.exr", 8, 8, 48, false},
        FixtureExpectation{"multipart_deeptile_flat.exr", 8, 8, 48, false},
        FixtureExpectation{"multiview_string.exr", 8, 8, 48, false},
        FixtureExpectation{"multiview_left_first.exr", 8, 8, 48, false},
        FixtureExpectation{"multiview_right_first.exr", 8, 8, 48, false},
        FixtureExpectation{"tiled_rgba_3x3.exr", 8, 8, 64, true},
        FixtureExpectation{"tiled_data_offset.exr", 16, 16, 48, false},
        FixtureExpectation{"chroma_zips.exr", 16, 8, 64, true},
        FixtureExpectation{"chroma_extra_channel.exr", 16, 8, 48, false},
        FixtureExpectation{"chroma_missing_by.exr", 16, 8, 48, false},
        FixtureExpectation{"yc_whitey0.exr", 16, 8, 48, false},
        FixtureExpectation{"yc_collinear.exr", 16, 8, 48, false},
        FixtureExpectation{"t01.exr", 400, 300, 48, false},
        FixtureExpectation{"t02.exr", 400, 300, 48, false},
        FixtureExpectation{"t05.exr", 340, 260, 48, false},
        FixtureExpectation{"t07.exr", 481, 371, 48, false},
        FixtureExpectation{"t09.exr", 200, 300, 48, false},
        FixtureExpectation{"t13.exr", 101, 101, 48, false},
        FixtureExpectation{"t14.exr", 101, 101, 48, false},
        FixtureExpectation{"AllHalfValues.exr", 256, 256, 48, false},
        FixtureExpectation{"BrightRingsNanInf.exr", 800, 800, 48, false},
        FixtureExpectation{"GammaChart.exr", 800, 800, 48, false},
        FixtureExpectation{"GrayRampsHorizontal.exr", 800, 800, 48, false},
        FixtureExpectation{"WideColorGamut.exr", 800, 800, 48, false},
        FixtureExpectation{"WideFloatRange.exr", 500, 500, 96, false},
        FixtureExpectation{"stripes.exr", 100, 50, 64, true},
        FixtureExpectation{"Garden.exr", 874, 493, 48, false},
        FixtureExpectation{"ColorCodedLevels.exr", 512, 512, 64, true},
        FixtureExpectation{"PeriodicPattern.exr", 517, 517, 48, false},
        FixtureExpectation{"Rec709_YC.exr", 610, 406, 48, false},
        FixtureExpectation{"XYZ_YC.exr", 610, 406, 48, false},
    };

    // Refused at open: not recognised, malformed, nothing displayable, beyond the limits.
    inline constexpr std::array<std::string_view, 12> kRejected{
        "garbage.bin",
        "not_exr.txt",
        "magic_only.exr",
        "uint_only.exr",
        "deep_only.exr",
        "huge_display.exr",
        "display_over_maxpixels.exr",
        "tile_too_large.exr",
        "truncated.exr",
        "corrupt_chunk.exr",
        "chroma_corrupt_chunk.exr",
        "yc_huge_data.exr",
    };
    inline constexpr std::array<std::string_view, 0> kDecodeFailures{};

} // namespace pvdkit::exr::tests
