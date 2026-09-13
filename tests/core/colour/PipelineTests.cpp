#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include <doctest/doctest.h>

#include "core/colour/Pipeline.hpp"
#include "core/colour/Primaries.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::core::colour
{
    namespace
    {

        TEST_CASE("Presentation identity rule preserves every sRGB-ish signalling combination")
        {
            for (const auto primaries : std::array<std::uint16_t, 2>{1, 2}) {
                for (const auto transfer : std::array<std::uint16_t, 6>{1, 2, 6, 13, 14, 15}) {
                    CAPTURE(primaries);
                    CAPTURE(transfer);
                    CHECK_FALSE(Presentation::needed(Cicp{primaries, transfer, 9, false}));
                }
            }

            CHECK(Presentation::needed(Cicp{9, 13, 9, false}));
            for (const auto transfer : std::array<std::uint16_t, 5>{4, 5, 8, 16, 18}) {
                CAPTURE(transfer);
                CHECK(Presentation::needed(Cicp{1, transfer, 1, false}));
            }
            CHECK_FALSE(Presentation::needed(Cicp{1, 99, 1, false}));
            CHECK(Presentation::needed(Cicp{99, 13, 1, false}));
        }

        TEST_CASE("an identity Presentation leaves BGRA64 rows byte-identical")
        {
            std::array<std::uint16_t, 8> row{0, 1, 32'768, 12'345, 65'535, 42, 9'999, 54'321};
            const auto before = row;
            const Presentation presentation{Cicp{1, 13, 6, false}, std::nullopt};

            presentation.apply(row);

            CHECK(row == before);

            std::array<std::byte, 8> byteRow{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                             std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
            const auto beforeBytes = byteRow;
            presentation.apply(byteRow);
            CHECK(byteRow == beforeBytes);
        }

        TEST_CASE("linear and P3-D65 samples are converted to encoded sRGB in BGRA order")
        {
            SUBCASE("linear transfer")
            {
                std::array<std::uint16_t, 4> row{16'384, 32'768, 65'535, 12'345};
                const Presentation presentation{Cicp{1, 8, 0, true}, std::nullopt};
                presentation.apply(row);
                CHECK(row[0] == doctest::Approx(35'199).epsilon(1.0e-4));
                CHECK(row[1] == doctest::Approx(48'192).epsilon(1.0e-4));
                CHECK(row[2] == 65'535);
                CHECK(row[3] == 12'345);
            }

            SUBCASE("P3-D65 primaries")
            {
                // Source linear RGB is (0.5, 0.25, 0.125); the fixed expected samples were
                // independently calculated from the SMPTE EG 432-1 matrix in PrimariesTests.
                std::array<std::uint16_t, 4> row{8'192, 16'384, 32'768, 60'000};
                const Presentation presentation{Cicp{12, 8, 0, true}, std::nullopt};
                presentation.apply(row);
                CHECK(row[0] == doctest::Approx(23'727).epsilon(1.0e-4));
                CHECK(row[1] == doctest::Approx(34'510).epsilon(1.0e-4));
                CHECK(row[2] == doctest::Approx(50'544).epsilon(1.0e-4));
                CHECK(row[3] == 60'000);
            }
        }

        TEST_CASE("PQ is tone-mapped with its mastering peak and quantized to sRGB")
        {
            const auto pq100Nits = static_cast<std::uint16_t>(std::lround(0.5080784215F * 65'535.0F));
            std::array<std::uint16_t, 4> row{pq100Nits, pq100Nits, pq100Nits, 44'444};
            const Presentation presentation{Cicp{1, 16, 0, true}, 1'000.0F};

            presentation.apply(row);

            CHECK(presentation.sourcePeakNits() == 1'000.0F);
            CHECK(row[0] == doctest::Approx(55'866).epsilon(2.0e-4));
            CHECK(row[1] == row[0]);
            CHECK(row[2] == row[0]);
            CHECK(row[3] == 44'444);
        }

        TEST_CASE("HLG applies its 1000-nit OOTF and ignores mastering metadata")
        {
            std::array<std::uint16_t, 4> row{32'768, 32'768, 32'768, 65'535};
            const Presentation presentation{Cicp{9, 18, 9, false}, 400.0F};
            presentation.apply(row);

            CHECK(presentation.sourcePeakNits() == 1'000.0F);
            CHECK(row[0] == row[1]);
            CHECK(row[1] == row[2]);
            CHECK(row[0] > 0);
            CHECK(row[0] < 65'535);

            std::array<std::uint16_t, 4> black{0, 0, 0, 12'345};
            presentation.apply(black);
            CHECK(black == std::array<std::uint16_t, 4>{42, 42, 42, 12'345});
        }

        TEST_CASE("invalid and unavailable PQ mastering peaks use the 1000-nit fallback")
        {
            CHECK(Presentation{Cicp{9, 16, 9, false}, std::nullopt}.sourcePeakNits() == 1'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, -1.0F}.sourcePeakNits() == 1'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, std::numeric_limits<float>::quiet_NaN()}.sourcePeakNits() ==
                  1'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, 20'000.0F}.sourcePeakNits() == 10'000.0F);
            CHECK(Presentation{Cicp{9, 16, 9, false}, 600.0F}.sourcePeakNits() == 600.0F);
        }

        TEST_CASE("PQ transfer and P3-D65 primaries agree with independent zscale SDR-range samples")
        {
            struct ReferenceSample
            {
                Primaries::Rgb encoded;
                Primaries::Rgb linearBt709;
            };
            // cosmos1650_yuv444_10bpc_p3pq.avif was decoded by zscale to encoded P3/PQ GBR float,
            // then independently converted to linear BT.709 with npl=100. Both selected output
            // pixels are wholly within [0, 1], and tone mapping is deliberately absent.
            constexpr std::array samples{
                ReferenceSample{{0.401375532F, 0.292456090F, 0.172915980F},
                                {0.382961124F, 0.0811400041F, 0.00322370953F}},
                ReferenceSample{{0.494066209F, 0.439880162F, 0.320413500F},
                                {0.952770710F, 0.482266605F, 0.0859083757F}},
            };
            constexpr auto tolerance = 2.0F / 255.0F;

            for (const auto &sample : samples) {
                const Primaries::Rgb sourceNits{Transfer::toLinear(16, sample.encoded[0]),
                                                Transfer::toLinear(16, sample.encoded[1]),
                                                Transfer::toLinear(16, sample.encoded[2])};
                auto actual = Primaries::apply(Primaries::toSrgb(12), sourceNits);
                for (auto &channel : actual) {
                    channel /= 100.0F;
                }

                for (std::size_t channel = 0; channel < actual.size(); ++channel) {
                    CAPTURE(channel);
                    CHECK(sample.linearBt709[channel] >= 0.0F);
                    CHECK(sample.linearBt709[channel] <= 1.0F);
                    CHECK(std::abs(actual[channel] - sample.linearBt709[channel]) <= tolerance);
                }
            }
        }

    } // namespace
} // namespace pvdkit::core::colour
