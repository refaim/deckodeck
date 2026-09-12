#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace pvdkit::rpgmvp::tests
{

    struct FixtureExpectation
    {
        std::string_view name;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t sourceBpp;
        bool alpha;
    };

    inline constexpr std::array kAccepted{
        FixtureExpectation{"rgba8_36x16_shadow2.rpgmvp", 36, 16, 32, true},
        FixtureExpectation{"indexed4_trns_144x192_cursor.rpgmvp", 144, 192, 4, true},
        FixtureExpectation{"indexed8_trns_288x384_weapons3.rpgmvp", 288, 384, 8, true},
        FixtureExpectation{"rgb8_816x624_gameover.rpgmvp", 816, 624, 24, false},
        FixtureExpectation{"rgba16_60x20_par.rpgmvp", 60, 20, 64, true},
        FixtureExpectation{"indexed8_trns_82x38_shadow2.png_", 82, 38, 8, true},
        FixtureExpectation{"rgba8_576x384_lolded.png_", 576, 384, 32, true},
        FixtureExpectation{"rgba8_adam7_700x700_kamen.png_", 700, 700, 32, true},
        FixtureExpectation{"rgb16_88x4a.rpgmvp", 88, 4, 48, false},
        FixtureExpectation{"rgba8_48x48.rpgmvp", 48, 48, 32, true},
        FixtureExpectation{"rgba8_srgb_48x48.rpgmvp", 48, 48, 32, true},
        FixtureExpectation{"gray8_16x8.rpgmvp", 16, 8, 24, false},
        FixtureExpectation{"graya8_16x8.rpgmvp", 16, 8, 32, true},
        FixtureExpectation{"gray1_16x8.rpgmvp", 16, 8, 3, false},
        FixtureExpectation{"rgba8_noninterlaced_700x700_kamen.rpgmvp", 700, 700, 32, true},
        FixtureExpectation{"apng_4x4.rpgmvp", 4, 4, 24, false},
    };

    inline constexpr std::array<std::string_view, 7> kRejected{
        "rgba8_48x48.png",
        "rgb16_88x4a.png",
        "stub_31.bin",
        "stub_48.bin",
        "bad_ihdr_crc.rpgmvp",
        "too_large_100000x100000.rpgmvp",
        "decode-failures/bad_srgb_crc.rpgmvp",
    };
    inline constexpr std::array<std::string_view, 1> kDecodeFailures{
        "decode-failures/truncated_idat.rpgmvp",
    };

} // namespace pvdkit::rpgmvp::tests
