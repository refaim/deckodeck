#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <doctest/doctest.h>

#include "../Fakes.hpp"
#include "core/colour/Pipeline.hpp"

namespace pvdkit::core::colour
{
    namespace
    {

        std::vector<std::uint16_t> codedPixels(const std::size_t count)
        {
            std::vector<std::uint16_t> pixels(count * 4);
            for (std::size_t index = 0; index < count; ++index) {
                pixels[index * 4] = static_cast<std::uint16_t>((index * 541U) % 65'536U);
                pixels[index * 4 + 1] = static_cast<std::uint16_t>((index * 229U) % 65'536U);
                pixels[index * 4 + 2] = static_cast<std::uint16_t>((index * 827U) % 65'536U);
                pixels[index * 4 + 3] = static_cast<std::uint16_t>((index * 997U) % 65'536U);
            }
            return pixels;
        }

        void reverseRedBlue(std::span<std::uint16_t> pixels)
        {
            for (std::size_t index = 0; index < pixels.size(); index += 4) {
                std::swap(pixels[index], pixels[index + 2]);
            }
        }

        std::vector<std::uint16_t> withoutAlpha(const std::span<const std::uint16_t> rgba)
        {
            std::vector<std::uint16_t> rgb;
            rgb.reserve(rgba.size() / 4 * 3);
            for (std::size_t index = 0; index < rgba.size(); index += 4) {
                rgb.insert(rgb.end(), rgba.begin() + static_cast<std::ptrdiff_t>(index),
                           rgba.begin() + static_cast<std::ptrdiff_t>(index + 3));
            }
            return rgb;
        }

    } // namespace

    TEST_CASE("RGBA presentation matches BGRA for identity SDR PQ HLG and explicit primaries")
    {
        constexpr std::array profiles{Cicp{1, 13, 6, false}, Cicp{12, 13, 6, false}, Cicp{9, 16, 9, false},
                                      Cicp{9, 18, 9, false}};
        for (const auto &cicp : profiles) {
            for (const bool explicitPrimaries : {false, true}) {
                const auto primaries = explicitPrimaries ? std::optional{*Primaries::chromaticities(12)}
                                                         : std::optional<Primaries::Chromaticities>{};
                Presentation presentation(cicp, {}, primaries, test::outputTables());
                auto bgra = codedPixels(131);
                auto rgbaSamples = bgra;
                reverseRedBlue(rgbaSamples);
                auto rgbaBytes = rgbaSamples;
                presentation.apply(std::span{bgra});
                presentation.apply(std::span{rgbaSamples}, true);
                presentation.apply(std::as_writable_bytes(std::span{rgbaBytes}), true);
                reverseRedBlue(bgra);
                CHECK(rgbaSamples == bgra);
                CHECK(rgbaBytes == bgra);
                const auto original = codedPixels(131);
                for (std::size_t index = 3; index < bgra.size(); index += 4) {
                    CHECK(bgra[index] == original[index]);
                }
            }
        }
    }

    TEST_CASE("RGBA whole image presentation keeps serial and parallel row bands identical")
    {
        constexpr std::uint32_t width = 1'024;
        constexpr std::uint32_t height = 257;
        Presentation presentation(Cicp{9, 16, 9, false}, {}, test::outputTables());
        auto serial = codedPixels(static_cast<std::size_t>(width) * height);
        auto parallel = serial;
        presentation.applyImage(std::as_writable_bytes(std::span{serial}), width * 8, height, 1, true);
        presentation.applyImage(std::as_writable_bytes(std::span{parallel}), width * 8, height, 4, true);
        CHECK(parallel == serial);
    }

    TEST_CASE("RGB48 presentation exactly matches RGBA64 through every colour path and block boundary")
    {
        constexpr std::array profiles{Cicp{1, 13, 6, false}, Cicp{12, 13, 6, false}, Cicp{9, 16, 9, false},
                                      Cicp{9, 18, 9, false}};
        for (const auto &cicp : profiles) {
            for (const bool explicitPrimaries : {false, true}) {
                const auto primaries = explicitPrimaries ? std::optional{*Primaries::chromaticities(12)}
                                                         : std::optional<Primaries::Chromaticities>{};
                Presentation presentation(cicp, {}, primaries, test::outputTables());
                for (const auto count : {0U, 1U, 63U, 64U, 65U, 131U}) {
                    auto rgba = codedPixels(count);
                    auto samples = withoutAlpha(rgba);
                    auto bytes = samples;
                    presentation.apply(std::span{rgba}, true);
                    presentation.apply(std::span{samples}, true, 3);
                    presentation.apply(std::as_writable_bytes(std::span{bytes}), true, 3);
                    CHECK(samples == withoutAlpha(rgba));
                    CHECK(bytes == samples);
                }
            }
        }
    }

    TEST_CASE("RGB48 whole image presentation uses six byte pixels in serial and parallel bands")
    {
        constexpr std::uint32_t width = 1'024;
        constexpr std::uint32_t height = 257;
        Presentation presentation(Cicp{9, 16, 9, false}, {}, test::outputTables());
        auto rgba = codedPixels(static_cast<std::size_t>(width) * height);
        auto serial = withoutAlpha(rgba);
        auto parallel = serial;
        presentation.applyImage(std::as_writable_bytes(std::span{rgba}), width * 8, height, 1, true);
        presentation.applyImage(std::as_writable_bytes(std::span{serial}), width * 6, height, 1, true, 3);
        presentation.applyImage(std::as_writable_bytes(std::span{parallel}), width * 6, height, 4, true, 3);
        CHECK(serial == withoutAlpha(rgba));
        CHECK(parallel == serial);
    }

} // namespace pvdkit::core::colour
