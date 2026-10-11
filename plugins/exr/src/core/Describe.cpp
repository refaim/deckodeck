#include "core/Describe.hpp"

#include <array>
#include <cstddef>

namespace pvdkit::exr
{
    namespace
    {
        // Indexed by Compression's enumerator order, which is the file format's own numbering.
        constexpr std::array kCompressionNames{
            std::string_view{"uncompressed"}, std::string_view{"RLE"},   std::string_view{"ZIPS"},
            std::string_view{"ZIP"},          std::string_view{"PIZ"},   std::string_view{"PXR24"},
            std::string_view{"B44"},          std::string_view{"B44A"},  std::string_view{"DWAA"},
            std::string_view{"DWAB"},         std::string_view{"HTJ2K"}, std::string_view{"HTJ2K"},
        };

        std::string channelsItem(const ChannelSelection &selection)
        {
            std::string item{selection.type == PixelType::Float ? "float " : "half "};
            switch (selection.kind) {
            case ChannelKind::Rgb:
                return item + (selection.hasAlpha() ? "RGBA" : "RGB");
            case ChannelKind::LuminanceChroma:
                return item + (selection.hasAlpha() ? "Y+chroma+A" : "Y+chroma");
            case ChannelKind::Luminance:
                return item + (selection.hasAlpha() ? "YA" : "Y");
            case ChannelKind::Single:
                return item + "grey";
            }
            return item;
        }

        std::string colourItem(const ColourSignal &colour)
        {
            if (colour.unusable) {
                return "linear Rec.709 assumed";
            }
            if (colour.match == PrimariesMatch::Custom) {
                return "linear custom";
            }
            return "linear " + std::string{primariesName(colour.match)};
        }
    } // namespace

    std::string_view compressionName(const Compression compression) noexcept
    {
        return kCompressionNames[static_cast<std::size_t>(compression)];
    }

    std::string describeSource(const SourceFacts &facts)
    {
        if (facts.pageCount > 1) {
            std::string line = std::to_string(facts.pageCount) + " pages";
            if (facts.partCount > 1) {
                line += ", multipart";
            }
            if (facts.stereo) {
                line += ", multiview";
            } else if (facts.partCount == 1) {
                line += ", layers";
            }
            return line;
        }
        return channelsItem(facts.selection) + ", " + colourItem(facts.colour);
    }

    std::string presentationNote(const core::ImageMeta &)
    {
        return "→ sRGB";
    }

    std::string describe(const core::ImageMeta &meta)
    {
        return meta.sourceDetail + " " + presentationNote(meta);
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        // PVD comments and compression belong to the file; page zero cannot describe other parts.
        return core::ImageDescription{"OpenEXR", meta.frameCount > 1 ? std::string{} : meta.compression,
                                      exr::describe(meta)};
    }
} // namespace pvdkit::exr
