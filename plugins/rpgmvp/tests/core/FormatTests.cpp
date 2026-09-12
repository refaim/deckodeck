#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <doctest/doctest.h>

#include "core/Format.hpp"

namespace pvdkit::rpgmvp
{
    namespace
    {

        std::array<std::byte, 49> validHead()
        {
            std::array<std::byte, 49> head{};
            constexpr std::array signature{std::byte{0x52}, std::byte{0x50}, std::byte{0x47}, std::byte{0x4d},
                                           std::byte{0x56}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
            std::ranges::copy(signature, head.begin());
            head[8] = std::byte{0x00};
            head[9] = std::byte{0x03};
            head[10] = std::byte{0x01};

            constexpr std::array ihdrData{
                std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x30}, // width 48
                std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x30}, // height 48
                std::byte{0x08}, std::byte{0x06}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            };
            std::ranges::copy(ihdrData, head.begin() + 32);
            // CRC32("IHDR" + the 13 bytes above) = 0x5702f987.
            head[45] = std::byte{0x57};
            head[46] = std::byte{0x02};
            head[47] = std::byte{0xf9};
            head[48] = std::byte{0x87};
            return head;
        }

        TEST_CASE("CRC32 matches the standard empty and check vectors")
        {
            CHECK(crc32({}) == 0x00000000U);
            constexpr std::string_view check = "123456789";
            CHECK(crc32(std::as_bytes(std::span{check})) == 0xcbf43926U);
        }

        TEST_CASE("detection accepts the minimum valid header independently of RPG Maker version bytes")
        {
            auto head = validHead();
            CHECK(recognises(head));

            std::ranges::fill(std::span{head}.subspan(8, 8), std::byte{0xa5});
            CHECK(recognises(head));
        }

        TEST_CASE("detection rejects short input plain PNG wrong magic and a bad IHDR CRC")
        {
            const auto valid = validHead();
            CHECK_FALSE(recognises({}));
            CHECK_FALSE(recognises(std::span{valid}.first(31)));
            CHECK_FALSE(recognises(std::span{valid}.first(48)));

            auto plainPng = valid;
            std::ranges::copy(kPngHeader, plainPng.begin());
            CHECK_FALSE(recognises(plainPng));

            for (std::size_t index = 0; index < 8; ++index) {
                auto wrongMagic = valid;
                wrongMagic[index] ^= std::byte{0xff};
                CHECK_FALSE(recognises(wrongMagic));
            }

            auto badCrc = valid;
            badCrc[48] ^= std::byte{0x01};
            CHECK_FALSE(recognises(badCrc));
        }

    } // namespace
} // namespace pvdkit::rpgmvp
