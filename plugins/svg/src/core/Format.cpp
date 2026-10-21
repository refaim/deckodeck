#include "core/Format.hpp"

#include <cstdint>

namespace pvdkit::svg
{
    namespace
    {

        constexpr std::size_t kMinimumCandidateBytes = 2;
        constexpr std::size_t kUtf8BomBytes = 3;

        bool isXmlWhitespace(const std::byte value) noexcept
        {
            const auto character = static_cast<char>(std::to_integer<unsigned char>(value));
            return character == ' ' || character == '\t' || character == '\r' || character == '\n';
        }

        bool hasUtf8Bom(const std::span<const std::byte> head) noexcept
        {
            return head.size() >= kUtf8BomBytes && std::to_integer<std::uint8_t>(head[0]) == 0xEFU &&
                   std::to_integer<std::uint8_t>(head[1]) == 0xBBU && std::to_integer<std::uint8_t>(head[2]) == 0xBFU;
        }

    } // namespace

    bool isGzip(const std::span<const std::byte> input) noexcept
    {
        return input.size() >= 2 && std::to_integer<std::uint8_t>(input[0]) == 0x1FU &&
               std::to_integer<std::uint8_t>(input[1]) == 0x8BU;
    }

    bool recognises(std::span<const std::byte> head) noexcept
    {
        if (head.size() < kMinimumCandidateBytes) {
            return false;
        }
        if (isGzip(head)) {
            return true;
        }

        std::size_t offset = hasUtf8Bom(head) ? kUtf8BomBytes : 0;
        while (offset < head.size() && isXmlWhitespace(head[offset])) {
            ++offset;
        }
        if (offset == head.size()) {
            return true;
        }
        return std::to_integer<std::uint8_t>(head[offset]) == static_cast<std::uint8_t>('<');
    }

} // namespace pvdkit::svg
