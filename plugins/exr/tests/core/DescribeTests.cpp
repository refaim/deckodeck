#include <ostream>
#include <string>

#include <doctest/doctest.h>

#include "core/Channels.hpp"
#include "core/Colour.hpp"
#include "core/Describe.hpp"
#include "core/IDecoder.hpp"

namespace pvdkit::exr
{
    namespace
    {
        SourceFacts baseline()
        {
            SourceFacts facts;
            facts.selection = ChannelSelection{ChannelKind::Rgb, "", {"R", "G", "B"}, "A", PixelType::Half};
            facts.colour = ColourSignal{PrimariesMatch::Rec709, 1, std::nullopt, std::nullopt};
            facts.compression = Compression::Zip;
            facts.display = Box{0, 0, 1919, 1079};
            facts.data = facts.display;
            return facts;
        }

        TEST_CASE("every compression scheme has its file-format name")
        {
            CHECK(compressionName(Compression::None) == "uncompressed");
            CHECK(compressionName(Compression::Rle) == "RLE");
            CHECK(compressionName(Compression::Zips) == "ZIPS");
            CHECK(compressionName(Compression::Zip) == "ZIP");
            CHECK(compressionName(Compression::Piz) == "PIZ");
            CHECK(compressionName(Compression::Pxr24) == "PXR24");
            CHECK(compressionName(Compression::B44) == "B44");
            CHECK(compressionName(Compression::B44a) == "B44A");
            CHECK(compressionName(Compression::Dwaa) == "DWAA");
            CHECK(compressionName(Compression::Dwab) == "DWAB");
            CHECK(compressionName(Compression::Htj2k256) == "HTJ2K");
            CHECK(compressionName(Compression::Htj2k32) == "HTJ2K");
        }

        TEST_CASE("single-page source summaries omit geometry compression exposure and channel prefixes")
        {
            CHECK(describeSource(baseline()) == "half RGBA, linear Rec.709");
            auto facts = baseline();
            facts.selection = ChannelSelection{
                ChannelKind::Rgb, "beauty", {"beauty.R", "beauty.G", "beauty.B"}, "", PixelType::Float};
            facts.compression = Compression::Piz;
            facts.whiteLuminance = 1.0e30F;
            facts.display = Box{-40, -40, 440, 330};
            facts.data = Box{5, 3, 404, 302};
            facts.colorInteropID = "lin_rec709";
            facts.tiled = true;
            facts.levelMode = LevelMode::Ripmap;
            facts.tileWidth = 64;
            facts.levelsX = 5;
            facts.levelsY = 4;
            CHECK(describeSource(facts) == "float RGB, linear Rec.709");
        }

        TEST_CASE("single-page channel summaries distinguish colour luminance chroma and grey")
        {
            auto facts = baseline();
            facts.selection =
                ChannelSelection{ChannelKind::LuminanceChroma, "", {"Y", "RY", "BY"}, "", PixelType::Half};
            CHECK(describeSource(facts) == "half Y+chroma, linear Rec.709");
            facts.selection.alpha = "A";
            CHECK(describeSource(facts) == "half Y+chroma+A, linear Rec.709");
            facts.selection = ChannelSelection{ChannelKind::Luminance, "", {"Y", "", ""}, "", PixelType::Float};
            CHECK(describeSource(facts) == "float Y, linear Rec.709");
            facts.selection.alpha = "A";
            CHECK(describeSource(facts) == "float YA, linear Rec.709");
            facts.selection = ChannelSelection{ChannelKind::Single, "", {"Z", "", ""}, "", PixelType::Float};
            CHECK(describeSource(facts) == "float grey, linear Rec.709");
            facts.selection.kind = static_cast<ChannelKind>(255);
            CHECK(describeSource(facts) == "float , linear Rec.709");
        }

        TEST_CASE("single-page colour summaries retain known custom and assumed colour distinctions")
        {
            auto facts = baseline();
            facts.colour = ColourSignal{PrimariesMatch::AcesAp0, 2, kAcesAp0, std::nullopt};
            CHECK(describeSource(facts) == "half RGBA, linear ACES AP0");
            facts.colour = ColourSignal{PrimariesMatch::Rec2020, 9, std::nullopt, std::nullopt};
            CHECK(describeSource(facts) == "half RGBA, linear Rec.2020");
            facts.colour = ColourSignal{PrimariesMatch::Custom, 2, kAcesAp0, std::nullopt};
            CHECK(describeSource(facts) == "half RGBA, linear custom");
            facts.colour = ColourSignal{PrimariesMatch::Rec709, 1, std::nullopt,
                                        core::colour::Primaries::Chromaticities{
                                            {0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.0F}}};
            CHECK(describeSource(facts) == "half RGBA, linear Rec.709 assumed");
        }

        TEST_CASE("presentation summaries name the output without repeating tone-map diagnostics")
        {
            core::ImageMeta meta;
            meta.cicp = core::Cicp{1, 16, 0, true};
            meta.masteringPeakNits = 1'250.0F;
            CHECK(presentationNote(meta) == "→ sRGB");
            meta.masteringPeakNits = 100.0F;
            CHECK(presentationNote(meta) == "→ sRGB");
            meta.sourceDetail = "half RGB, linear Rec.709";
            CHECK(exr::describe(meta) == "half RGB, linear Rec.709 → sRGB");
        }

        TEST_CASE("multipage source summaries describe the file without asserting one page's properties")
        {
            auto facts = baseline();
            facts.pageCount = 2;
            facts.partCount = 2;
            facts.partName = "beauty";
            CHECK(describeSource(facts) == "2 pages, multipart");
            facts.stereo = true;
            facts.view = "left";
            CHECK(describeSource(facts) == "2 pages, multipart, multiview");
            facts.partCount = 1;
            CHECK(describeSource(facts) == "2 pages, multiview");
            facts.stereo = false;
            CHECK(describeSource(facts) == "2 pages, layers");
            facts.selection.type = PixelType::Float;
            facts.colour = ColourSignal{PrimariesMatch::AcesAp0, 2, kAcesAp0, std::nullopt};
            CHECK(describeSource(facts) == "2 pages, layers");
        }

        TEST_CASE("single-page describer retains compression in its dedicated host field")
        {
            core::ImageMeta meta;
            meta.frameCount = 1;
            meta.compression = "PIZ";
            meta.sourceDetail = "half RGBA, linear Rec.709";
            const auto description = Describer{}.describe(meta);
            CHECK(description.formatName == "OpenEXR");
            CHECK(description.compression == "PIZ");
            CHECK(description.comments == "half RGBA, linear Rec.709 → sRGB");
        }

        TEST_CASE("multipage describer does not present the first page's compression as a file-wide fact")
        {
            core::ImageMeta meta;
            meta.frameCount = 2;
            meta.compression = "PIZ";
            meta.sourceDetail = "2 pages, multipart, multiview";
            const auto description = Describer{}.describe(meta);
            CHECK(description.formatName == "OpenEXR");
            CHECK(description.compression.empty());
            CHECK(description.comments == "2 pages, multipart, multiview → sRGB");
        }
    } // namespace
} // namespace pvdkit::exr
