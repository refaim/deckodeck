#include "core/Describe.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/Encode.hpp"
#include "core/colour/Pipeline.hpp"

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

        std::string fixed4(const float value)
        {
            std::array<char, 32> buffer{};
            const auto result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, 4);
            return std::string{buffer.data(), result.ptr};
        }

        std::string coordinates(const core::colour::Primaries::Chromaticity &chromaticity)
        {
            return fixed4(chromaticity.x) + "," + fixed4(chromaticity.y);
        }

        std::string channelsItem(const ChannelSelection &selection)
        {
            std::string item{selection.type == PixelType::Float ? "float " : "half "};
            switch (selection.kind) {
            case ChannelKind::Rgb:
                item += selection.hasAlpha() ? "RGBA" : "RGB";
                if (!selection.layer.empty()) {
                    item += " (layer " + selection.layer + ")";
                }
                return item;
            case ChannelKind::LuminanceChroma:
                return item + (selection.hasAlpha() ? "Y+chroma with alpha" : "Y+chroma");
            case ChannelKind::Luminance:
                return item + (selection.hasAlpha() ? "Y with alpha" : "Y");
            case ChannelKind::Single:
                return item + selection.colour[0] + " as grey";
            }
            return item;
        }

        std::string coordinates(const core::colour::Primaries::Chromaticities &set)
        {
            return "R " + coordinates(set.red) + " G " + coordinates(set.green) + " B " + coordinates(set.blue) +
                   " W " + coordinates(set.white);
        }

        std::string colourItem(const ColourSignal &colour)
        {
            if (colour.unusable) {
                return "linear Rec.709 assumed, unusable chromaticities (" + coordinates(*colour.unusable) + ")";
            }
            if (colour.match != PrimariesMatch::Custom) {
                return "linear " + std::string{primariesName(colour.match)};
            }
            const auto set = colour.chromaticities.value_or(core::colour::Primaries::Chromaticities{});
            return "linear, unknown chromaticities (custom: " + coordinates(set) + ")";
        }

        std::string layoutItem(const SourceFacts &facts)
        {
            if (!facts.tiled) {
                return "scanline";
            }
            std::string item = "tiled " + std::to_string(facts.tileWidth) + "x" + std::to_string(facts.tileHeight);
            switch (facts.levelMode) {
            case LevelMode::One:
                return item;
            case LevelMode::Mipmap:
                return item + ", " + std::to_string(facts.levelsX) + " mip levels";
            case LevelMode::Ripmap:
                return item + ", " + std::to_string(facts.levelsX) + "x" + std::to_string(facts.levelsY) +
                       " rip levels";
            }
            return item;
        }

        std::string windowItem(const std::string_view name, const Box &box, const bool alwaysOrigin)
        {
            std::string item =
                std::string{name} + " " + std::to_string(box.width()) + "x" + std::to_string(box.height());
            if (alwaysOrigin || box.xMin != 0 || box.yMin != 0) {
                item += " at " + std::to_string(box.xMin) + "," + std::to_string(box.yMin);
            }
            return item;
        }

        std::string whiteItem(const std::optional<float> whiteLuminance)
        {
            // The attribute is any finite positive float; rounding one beyond the integer range
            // would be undefined, so the value saturates first (a float this large has no fraction).
            constexpr auto kLargest = static_cast<float>(std::numeric_limits<long long>::max());
            const auto nits = std::min(nitsPerUnit(whiteLuminance), kLargest);
            const auto rounded = nits >= kLargest ? std::numeric_limits<long long>::max() : std::llround(nits);
            return std::to_string(rounded) + " nit white";
        }

        void append(std::string &line, const std::string &item)
        {
            line += ", ";
            line += item;
        }

    } // namespace

    std::string_view compressionName(const Compression compression) noexcept
    {
        return kCompressionNames[static_cast<std::size_t>(compression)];
    }

    std::string describeSource(const SourceFacts &facts)
    {
        std::string line = channelsItem(facts.selection);
        append(line, colourItem(facts.colour));
        if (!facts.colorInteropID.empty()) {
            append(line, "colorInteropID " + facts.colorInteropID);
        }
        append(line, std::string{compressionName(facts.compression)});
        append(line, layoutItem(facts));
        append(line, windowItem("display", facts.display, false));
        append(line, windowItem("data", facts.data, true));
        append(line, whiteItem(facts.whiteLuminance));
        if (facts.partCount > 1) {
            std::string parts = std::to_string(facts.partCount) + " parts (part " + std::to_string(facts.partIndex);
            if (!facts.partName.empty()) {
                parts += ": " + facts.partName;
            }
            append(line, parts + ")");
        }
        if (facts.stereo) {
            append(line, facts.view.empty() ? std::string{"stereo"} : "stereo (" + facts.view + " view)");
        }
        if (facts.deepSkipped) {
            append(line, "deep parts skipped");
        }
        return line;
    }

    std::string presentationNote(const core::ImageMeta &meta)
    {
        const auto peak = core::colour::sourcePeakNits(meta.cicp.transfer, meta.masteringPeakNits);
        const auto rounded = std::llround(peak);
        // At or below 100 nit the BT.2390 knee is inactive; only its display-black lift remains.
        if (rounded <= std::llround(kMinimumPeakNits)) {
            return "→ sRGB (SDR range, no highlight compression)";
        }
        return "→ sRGB (BT.2390 tone map from " + std::to_string(rounded) + " nit)";
    }

    std::string describe(const core::ImageMeta &meta)
    {
        return meta.sourceDetail + ", " + presentationNote(meta);
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        return core::ImageDescription{"OpenEXR", meta.compression, exr::describe(meta)};
    }

} // namespace pvdkit::exr
