#include "core/Describe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "core/colour/Pipeline.hpp"
#include "core/colour/Primaries.hpp"
#include "core/colour/Transfer.hpp"

namespace pvdkit::avif
{

    using core::ChromaFormat;
    using core::Cicp;
    using core::ImageMeta;
    using core::MirrorAxis;

    namespace
    {

        std::string_view chromaName(const ChromaFormat chroma)
        {
            // Indexed by ChromaFormat's enumerator order (Yuv444, Yuv422, Yuv420,
            // Yuv400); every legal value is in range, so no checked access is needed.
            constexpr std::array names{std::string_view{"YUV 4:4:4"}, std::string_view{"YUV 4:2:2"},
                                       std::string_view{"YUV 4:2:0"}, std::string_view{"YUV 4:0:0"}};
            return names[static_cast<std::size_t>(chroma)];
        }

        constexpr std::uint64_t cicpKey(const Cicp &cicp)
        {
            return (static_cast<std::uint64_t>(cicp.primaries) << 32) |
                   (static_cast<std::uint64_t>(cicp.transfer) << 16) | cicp.matrix;
        }

        std::string_view cicpSuffix(const Cicp &cicp)
        {
            switch (cicpKey(cicp)) {
            case cicpKey(Cicp{1, 13, 6, false}):
                return " (sRGB)";
            case cicpKey(Cicp{1, 1, 1, false}):
                return " (BT.709)";
            case cicpKey(Cicp{9, 16, 9, false}):
                return " (BT.2020 PQ)";
            case cicpKey(Cicp{9, 18, 9, false}):
                return " (BT.2020 HLG)";
            case cicpKey(Cicp{12, 16, 12, false}):
                return " (P3 PQ)";
            case cicpKey(Cicp{2, 2, 2, false}):
                return " (unspecified)";
            default:
                return {};
            }
        }

        void append(std::string &description, const std::string_view item)
        {
            description += ", ";
            description += item;
        }

        std::string_view primariesName(const std::uint16_t primaries)
        {
            constexpr std::array names{std::pair{std::uint16_t{4}, std::string_view{"BT.470M"}},
                                       std::pair{std::uint16_t{5}, std::string_view{"BT.601-625"}},
                                       std::pair{std::uint16_t{6}, std::string_view{"BT.601-625"}},
                                       std::pair{std::uint16_t{7}, std::string_view{"SMPTE 240M/BT.601-525"}},
                                       std::pair{std::uint16_t{9}, std::string_view{"Rec.2020"}},
                                       std::pair{std::uint16_t{11}, std::string_view{"P3-DCI"}},
                                       std::pair{std::uint16_t{12}, std::string_view{"P3-D65"}},
                                       std::pair{std::uint16_t{22}, std::string_view{"EBU 3213-E"}}};
            const auto match = std::ranges::find(names, primaries, &decltype(names)::value_type::first);
            return match->second;
        }

        std::string presentationNote(const ImageMeta &meta)
        {
            std::string detail;
            const auto addDetail = [&](const std::string_view item) {
                if (!detail.empty()) {
                    detail += "; ";
                }
                detail += item;
            };

            if (core::colour::Transfer::isHdr(meta.cicp.transfer)) {
                const auto kind = meta.cicp.transfer == 16 ? "PQ " : "HLG ";
                const auto peak = core::colour::sourcePeakNits(meta.cicp.transfer, meta.masteringPeakNits);
                addDetail("BT.2390 tone map from " + std::string{kind} +
                          std::to_string(static_cast<std::uint32_t>(std::lround(peak))) + " nit");
            } else if (!core::colour::Primaries::isIdentity(meta.cicp.primaries) &&
                       core::colour::Primaries::isKnown(meta.cicp.primaries)) {
                addDetail(std::string{primariesName(meta.cicp.primaries)} + " primaries");
            }

            if (!core::colour::Transfer::isKnown(meta.cicp.transfer)) {
                addDetail("unknown transfer " + std::to_string(meta.cicp.transfer) + " treated as sRGB");
            }
            if (!core::colour::Primaries::isKnown(meta.cicp.primaries)) {
                addDetail("unknown primaries " + std::to_string(meta.cicp.primaries) + " treated as BT.709");
            }
            return detail.empty() ? std::string{} : "→ sRGB (" + detail + ")";
        }

    } // namespace

    std::string describe(const ImageMeta &meta)
    {
        std::string description = std::to_string(meta.depth) + "-bit ";
        description += chromaName(meta.chroma);
        if (meta.chroma == ChromaFormat::Yuv400) {
            description += " (monochrome)";
        } else {
            description += meta.cicp.fullRange ? " (full range)" : " (limited range)";
        }

        append(description, "CICP " + std::to_string(meta.cicp.primaries) + "/" + std::to_string(meta.cicp.transfer) +
                                "/" + std::to_string(meta.cicp.matrix) + std::string{cicpSuffix(meta.cicp)});

        if (meta.hasAlpha) {
            append(description, meta.alphaPremultiplied ? "premultiplied alpha" : "straight alpha");
        }
        if (meta.animated) {
            append(description, std::to_string(meta.frameCount) + " frames");
        }
        if (meta.hasIcc) {
            append(description, "ICC");
        }
        if (meta.hasExif) {
            append(description, "EXIF");
        }
        if (meta.hasXmp) {
            append(description, "XMP");
        }
        if (meta.transforms.clap) {
            append(description, "clap");
        }
        if (meta.transforms.irotAngle != 0) {
            append(description, "irot " + std::to_string(meta.transforms.irotAngle));
        }
        if (meta.transforms.imir) {
            append(description, *meta.transforms.imir == MirrorAxis::TopBottom ? "imir top-bottom" : "imir left-right");
        }
        const auto note = presentationNote(meta);
        if (!note.empty()) {
            append(description, note);
        }
        return description;
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        return core::ImageDescription{"AVIF", "AV1", avif::describe(meta)};
    }

} // namespace pvdkit::avif
