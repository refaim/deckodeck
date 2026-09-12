#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include <doctest/doctest.h>

#include "adapters/spng/Decoder.hpp"
#include "core/Format.hpp"

namespace pvdkit::rpgmvp
{
    namespace
    {

        TEST_CASE("the libspng stream substitutes the PNG header and skips both RPG Maker header blocks")
        {
            std::array<std::byte, 40> file{};
            for (std::size_t index = 32; index < file.size(); ++index) {
                file[index] = std::byte{static_cast<unsigned char>(index)};
            }
            detail::Stream stream{file};
            CHECK(stream.logicalSize() == 24);
            CHECK(stream.position() == 0);

            std::array<std::byte, 24> reconstructed{};
            CHECK(stream.read(std::span{reconstructed}.first(5)));
            CHECK(stream.read(std::span{reconstructed}.subspan(5, 15)));
            CHECK(stream.read(std::span{reconstructed}.last(4)));

            std::array<std::byte, 24> expected{};
            std::ranges::copy(kPngHeader, expected.begin());
            std::ranges::copy(std::span{file}.subspan(32), expected.begin() + 16);
            CHECK(reconstructed == expected);
            CHECK(stream.position() == expected.size());
            CHECK(stream.read({}));
        }

        TEST_CASE("the libspng stream refuses a read beyond end without consuming bytes")
        {
            std::array<std::byte, 33> file{};
            detail::Stream stream{file};
            std::array<std::byte, 18> tooMuch{};
            CHECK_FALSE(stream.read(tooMuch));
            CHECK(stream.position() == 0);

            std::array<std::byte, 17> exact{};
            CHECK(stream.read(exact));
            CHECK(stream.position() == 17);
            std::array<std::byte, 1> one{};
            CHECK_FALSE(stream.read(one));
            CHECK(stream.position() == 17);
        }

    } // namespace
} // namespace pvdkit::rpgmvp
