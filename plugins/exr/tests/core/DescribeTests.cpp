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
            facts.data = Box{0, 0, 1919, 1079};
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

        TEST_CASE("the source line names channels colour compression layout windows and white")
        {
            CHECK(describeSource(baseline()) ==
                  "half RGBA, linear Rec.709, ZIP, scanline, display 1920x1080, data 1920x1080 at 0,0, 100 nit white");

            auto facts = baseline();
            facts.selection = ChannelSelection{ChannelKind::Rgb, "", {"R", "G", "B"}, "", PixelType::Float};
            facts.compression = Compression::Piz;
            facts.whiteLuminance = 250.0F;
            facts.display = Box{-40, -40, 440, 330};
            facts.data = Box{0, 0, 399, 299};
            CHECK(describeSource(facts) == "float RGB, linear Rec.709, PIZ, scanline, display 481x371 at -40,-40, "
                                           "data 400x300 at 0,0, 250 nit white");
            facts.display = Box{0, 5, 480, 375};
            CHECK(describeSource(facts).find(", display 481x371 at 0,5, ") != std::string::npos);
        }

        TEST_CASE("channel kinds and layers are spelled out")
        {
            auto facts = baseline();
            facts.selection = ChannelSelection{
                ChannelKind::Rgb, "beauty", {"beauty.R", "beauty.G", "beauty.B"}, "beauty.A", PixelType::Half};
            CHECK(describeSource(facts).starts_with("half RGBA (layer beauty), "));
            facts.selection =
                ChannelSelection{ChannelKind::LuminanceChroma, "", {"Y", "RY", "BY"}, "", PixelType::Half};
            CHECK(describeSource(facts).starts_with("half Y+chroma, "));
            facts.selection =
                ChannelSelection{ChannelKind::LuminanceChroma, "", {"Y", "RY", "BY"}, "A", PixelType::Half};
            CHECK(describeSource(facts).starts_with("half Y+chroma with alpha, "));
            facts.selection = ChannelSelection{ChannelKind::Luminance, "", {"Y", "", ""}, "", PixelType::Float};
            CHECK(describeSource(facts).starts_with("float Y, "));
            facts.selection = ChannelSelection{ChannelKind::Luminance, "", {"Y", "", ""}, "A", PixelType::Half};
            CHECK(describeSource(facts).starts_with("half Y with alpha, "));
            facts.selection = ChannelSelection{ChannelKind::Single, "", {"Z", "", ""}, "", PixelType::Float};
            CHECK(describeSource(facts).starts_with("float Z as grey, "));
            // A kind outside the enumeration (never produced) prints the sample type alone.
            facts.selection = ChannelSelection{static_cast<ChannelKind>(255), "", {"Z", "", ""}, "", PixelType::Half};
            CHECK(describeSource(facts).starts_with("half , "));
        }

        TEST_CASE("colour is named by its match and custom sets print their coordinates")
        {
            auto facts = baseline();
            facts.colour = ColourSignal{PrimariesMatch::AcesAp0, 2, kAcesAp0, std::nullopt};
            CHECK(describeSource(facts).find(", linear ACES AP0, ") != std::string::npos);
            facts.colour = ColourSignal{PrimariesMatch::Rec2020, 9, std::nullopt, std::nullopt};
            CHECK(describeSource(facts).find(", linear Rec.2020, ") != std::string::npos);
            facts.colour =
                ColourSignal{PrimariesMatch::Custom, 2,
                             core::colour::Primaries::Chromaticities{
                                 {0.7347F, 0.2653F}, {0.1596F, 0.8404F}, {0.0366F, 0.0001F}, {0.3457F, 0.3585F}},
                             std::nullopt};
            CHECK(
                describeSource(facts).find(", linear, unknown chromaticities (custom: R 0.7347,0.2653 G 0.1596,0.8404 "
                                           "B 0.0366,0.0001 W 0.3457,0.3585), ") != std::string::npos);
            facts.colour = ColourSignal{PrimariesMatch::CieXyz, 2,
                                        core::colour::Primaries::Chromaticities{
                                            {1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F / 3.0F, 1.0F / 3.0F}},
                                        std::nullopt};
            CHECK(describeSource(facts).find(", linear CIE XYZ, ") != std::string::npos);
            // An attribute no derivation can use: Rec.709 stands in, the values are still printed.
            facts.colour = ColourSignal{PrimariesMatch::Rec709, 1, std::nullopt,
                                        core::colour::Primaries::Chromaticities{
                                            {0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, {0.3127F, 0.0F}}};
            CHECK(describeSource(facts).find(", linear Rec.709 assumed, unusable chromaticities (R 0.6400,0.3300 "
                                             "G 0.3000,0.6000 B 0.1500,0.0600 W 0.3127,0.0000), ") !=
                  std::string::npos);
            facts.colorInteropID = "lin_ap0";
            CHECK(describeSource(facts).find(", colorInteropID lin_ap0, ") != std::string::npos);
        }

        TEST_CASE("the white luminance item rounds and clamps whatever the attribute says")
        {
            auto facts = baseline();
            facts.whiteLuminance = 249.6F;
            CHECK(describeSource(facts).find(", 250 nit white") != std::string::npos);
            // Beyond any integer: the item saturates instead of rounding an unrepresentable value.
            facts.whiteLuminance = 1.0e30F;
            CHECK(describeSource(facts).find(", 9223372036854775807 nit white") != std::string::npos);
        }

        TEST_CASE("tiles levels parts views and skipped deep parts are reported")
        {
            auto facts = baseline();
            facts.tiled = true;
            facts.tileWidth = 64;
            facts.tileHeight = 32;
            CHECK(describeSource(facts).find(", tiled 64x32, ") != std::string::npos);
            facts.levelMode = LevelMode::Mipmap;
            facts.levelsX = 5;
            facts.levelsY = 5;
            CHECK(describeSource(facts).find(", tiled 64x32, 5 mip levels, ") != std::string::npos);
            facts.levelMode = LevelMode::Ripmap;
            facts.levelsX = 5;
            facts.levelsY = 4;
            CHECK(describeSource(facts).find(", tiled 64x32, 5x4 rip levels, ") != std::string::npos);
            facts.levelMode = static_cast<LevelMode>(255);
            CHECK(describeSource(facts).find(", tiled 64x32, display") != std::string::npos);

            facts = baseline();
            facts.partCount = 2;
            facts.partIndex = 1;
            facts.partName = "beauty";
            CHECK(describeSource(facts).ends_with(", 100 nit white, 2 parts (part 1: beauty)"));
            facts.partName.clear();
            CHECK(describeSource(facts).ends_with(", 100 nit white, 2 parts (part 1)"));
            facts.stereo = true;
            facts.view = "left";
            CHECK(describeSource(facts).ends_with(", 2 parts (part 1), stereo (left view)"));
            facts.view.clear();
            CHECK(describeSource(facts).ends_with(", 2 parts (part 1), stereo"));
            facts.deepSkipped = true;
            CHECK(describeSource(facts).ends_with(", stereo, deep parts skipped"));
        }

        TEST_CASE("the presentation note follows the peak the decoder measured")
        {
            core::ImageMeta meta;
            meta.cicp = core::Cicp{1, 16, 0, true};
            meta.masteringPeakNits = 1'250.0F;
            CHECK(presentationNote(meta) == "→ sRGB (BT.2390 tone map from 1250 nit)");
            meta.masteringPeakNits = 100.0F;
            CHECK(presentationNote(meta) == "→ sRGB (SDR range, no highlight compression)");
            meta.masteringPeakNits = 100.4F;
            CHECK(presentationNote(meta) == "→ sRGB (SDR range, no highlight compression)");
            meta.masteringPeakNits = 10'000.0F;
            CHECK(presentationNote(meta) == "→ sRGB (BT.2390 tone map from 10000 nit)");

            meta.sourceDetail = "half RGB, linear Rec.709";
            CHECK(exr::describe(meta) == "half RGB, linear Rec.709, → sRGB (BT.2390 tone map from 10000 nit)");
        }

        TEST_CASE("the describer hands the host the format compression and comments")
        {
            core::ImageMeta meta;
            meta.cicp = core::Cicp{1, 16, 0, true};
            meta.masteringPeakNits = 400.0F;
            meta.compression = "PIZ";
            meta.sourceDetail = "half RGBA, linear Rec.709, PIZ, scanline";
            const Describer describer;
            const auto description = describer.describe(meta);
            CHECK(description.formatName == "OpenEXR");
            CHECK(description.compression == "PIZ");
            CHECK(description.comments ==
                  "half RGBA, linear Rec.709, PIZ, scanline, → sRGB (BT.2390 tone map from 400 nit)");
        }

    } // namespace
} // namespace pvdkit::exr
