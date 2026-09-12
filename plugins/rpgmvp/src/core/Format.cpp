#include "core/Format.hpp"

#include <algorithm>

namespace pvdkit::rpgmvp
{
    namespace
    {

        constexpr std::array kSignature{std::byte{0x52}, std::byte{0x50}, std::byte{0x47}, std::byte{0x4d},
                                        std::byte{0x56}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
        constexpr std::size_t kMinimumSize = 49;
        constexpr std::uint32_t kPolynomial = 0xedb88320U;

        std::uint32_t updateCrc(std::uint32_t crc, const std::span<const std::byte> bytes) noexcept
        {
            for (const std::byte value : bytes) {
                crc ^= std::to_integer<std::uint8_t>(value);
                for (unsigned bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? kPolynomial : 0U);
                }
            }
            return crc;
        }

        std::uint32_t readBigEndian(const std::span<const std::byte, 4> bytes) noexcept
        {
            return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8U) |
                   static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
        }

    } // namespace

    std::uint32_t crc32(const std::span<const std::byte> bytes) noexcept
    {
        return updateCrc(0xffffffffU, bytes) ^ 0xffffffffU;
    }

    bool recognises(const std::span<const std::byte> head) noexcept
    {
        if (head.size() < kMinimumSize || !std::ranges::equal(kSignature, head.first(kSignature.size()))) {
            return false;
        }

        std::uint32_t crc = updateCrc(0xffffffffU, std::span{kPngHeader}.subspan<12, 4>());
        crc = updateCrc(crc, head.subspan<32, 13>());
        const auto stored = readBigEndian(head.subspan<45, 4>());
        return (crc ^ 0xffffffffU) == stored;
    }

} // namespace pvdkit::rpgmvp
