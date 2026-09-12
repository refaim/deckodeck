#include "core/Describe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
        return description;
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        return core::ImageDescription{"AVIF", "AV1", avif::describe(meta)};
    }

} // namespace pvdkit::avif
