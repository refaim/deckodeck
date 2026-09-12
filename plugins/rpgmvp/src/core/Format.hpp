#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pvdkit::rpgmvp
{

    inline constexpr std::array kPngHeader{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47}, std::byte{0x0d}, std::byte{0x0a},
        std::byte{0x1a}, std::byte{0x0a}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0d},
        std::byte{0x49}, std::byte{0x48}, std::byte{0x44}, std::byte{0x52},
    };

    [[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] bool recognises(std::span<const std::byte> head) noexcept;

} // namespace pvdkit::rpgmvp
