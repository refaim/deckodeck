#include "core/Describe.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "core/colour/Primaries.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::avif
{
    namespace
    {
        std::string_view chromaName(const core::ChromaFormat chroma)
        {
            // ChromaFormat's enumerator order is Yuv444, Yuv422, Yuv420, Yuv400.
            constexpr std::array names{std::string_view{"4:4:4"}, std::string_view{"4:2:2"}, std::string_view{"4:2:0"},
                                       std::string_view{"grey"}};
            return names[static_cast<std::size_t>(chroma)];
        }

        std::string_view primariesName(const std::uint16_t primaries)
        {
            constexpr std::array names{std::pair{std::uint16_t{4}, std::string_view{"BT.470M"}},
                                       std::pair{std::uint16_t{5}, std::string_view{"BT.601"}},
                                       std::pair{std::uint16_t{6}, std::string_view{"BT.601"}},
                                       std::pair{std::uint16_t{7}, std::string_view{"SMPTE 240M"}},
                                       std::pair{std::uint16_t{9}, std::string_view{"Rec.2020"}},
                                       std::pair{std::uint16_t{11}, std::string_view{"P3-DCI"}},
                                       std::pair{std::uint16_t{12}, std::string_view{"P3-D65"}},
                                       std::pair{std::uint16_t{22}, std::string_view{"EBU 3213"}}};
            // Called only for known non-identity primaries, all present in this table.
            return std::ranges::find(names, primaries, &decltype(names)::value_type::first)->second;
        }

        std::string colourNote(const core::Cicp &cicp)
        {
            if (core::colour::Transfer::isHdr(cicp.transfer)) {
                return cicp.transfer == 16 ? "PQ → sRGB" : "HLG → sRGB";
            }
            if (!core::colour::Primaries::isIdentity(cicp.primaries) &&
                core::colour::Primaries::isKnown(cicp.primaries)) {
                return std::string{primariesName(cicp.primaries)} + " → sRGB";
            }
            switch (cicp.transfer) {
            case 1:
            case 6:
            case 14:
            case 15:
                return "BT.709";
            case 2:
                return "unspecified";
            case 4:
                return "gamma 2.2 → sRGB";
            case 5:
                return "gamma 2.8 → sRGB";
            case 8:
                return "linear → sRGB";
            default:
                return "sRGB";
            }
        }
    } // namespace

    std::string describe(const core::ImageMeta &meta)
    {
        std::string description = std::to_string(meta.depth) + "-bit " + std::string{chromaName(meta.chroma)};
        if (meta.hasAlpha) {
            description += meta.alphaPremultiplied ? ", premul alpha" : ", alpha";
        }
        description += ", " + colourNote(meta.cicp);
        if (!core::colour::Transfer::isKnown(meta.cicp.transfer) ||
            !core::colour::Primaries::isKnown(meta.cicp.primaries)) {
            description += " (fallback)";
        }
        return description;
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        return core::ImageDescription{"AVIF", "AV1", avif::describe(meta)};
    }
} // namespace pvdkit::avif
