#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace pvdkit::avif::tests
{

    struct FixtureExpectation
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t pages;
        std::uint32_t pageBpp;
        bool alpha;
        bool icc = false;
        bool presentation = false;
    };

    // Every fixture from plugins/avif/fixtures/SOURCES.md that libavif accepts; page sizes come
    // from that table (ffprobe), with the irot swap applied for rotated files.
    inline constexpr std::array kAccepted{
        FixtureExpectation{"white_1x1.avif", 1, 1, 1, 24, false},
        FixtureExpectation{"kodim03_yuv420_8bpc.avif", 768, 512, 1, 24, false},
        FixtureExpectation{"cosmos1650_yuv444_10bpc_p3pq.avif", 1024, 428, 1, 30, false, false, true},
        FixtureExpectation{"alpha_noispe.avif", 80, 80, 1, 32, true},
        FixtureExpectation{"abc_color_irot_alpha_irot.avif", 256, 512, 1, 32, true},
        FixtureExpectation{"abc_color_irot_alpha_NOirot.avif", 256, 512, 1, 32, true},
        FixtureExpectation{"clop_irot_imor.avif", 34, 12, 1, 40, true},
        FixtureExpectation{"sofa_grid1x5_420.avif", 1024, 770, 1, 24, false},
        FixtureExpectation{"color_grid_alpha_nogrid.avif", 80, 80, 1, 32, true},
        FixtureExpectation{"colors-animated-8bpc.avif", 150, 150, 5, 24, false},
        FixtureExpectation{"colors-animated-8bpc-alpha-exif-xmp.avif", 150, 150, 5, 32, true},
        FixtureExpectation{"colors-animated-12bpc-keyframes-0-2-3.avif", 64, 64, 5, 48, true},
        FixtureExpectation{"colors_hdr_rec2020.avif", 200, 200, 1, 30, false, false, true},
        FixtureExpectation{"colors_sdr_srgb.avif", 200, 200, 1, 24, false},
        FixtureExpectation{"paris_icc_exif_xmp.avif", 403, 302, 1, 24, false, true},
        FixtureExpectation{"draw_points_idat_progressive.avif", 33, 11, 1, 32, true, false, true},
        FixtureExpectation{"extended_pixi.avif", 4, 4, 1, 24, false},
        FixtureExpectation{"weld_sato_12B_8B_q0.avif", 1024, 684, 1, 36, false},
        FixtureExpectation{"quad_rgb_lossless.avif", 64, 64, 1, 24, false},
        FixtureExpectation{"quad_yuv420.avif", 64, 64, 1, 24, false},
        FixtureExpectation{"alpha_steps.avif", 96, 32, 1, 32, true},
        FixtureExpectation{"anim_3frames.avif", 64, 64, 3, 24, false},
        FixtureExpectation{"gray_400.avif", 64, 64, 1, 24, false},
        FixtureExpectation{"tenbit_444.avif", 64, 64, 1, 30, false},
    };

    inline constexpr std::array<std::string_view, 1> kUnsupported{
        "clap_irot_imir_non_essential.avif",
    };
    inline constexpr std::array<std::string_view, 4> kRejected{
        "not_avif.png",
        "not_avif.bmp",
        "garbage.bin",
        "truncated.avif",
    };
    inline constexpr std::array<std::string_view, 0> kDecodeFailures{};

} // namespace pvdkit::avif::tests
