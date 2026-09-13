#include <array>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

#include "core/Describe.hpp"

namespace pvdkit::avif
{

    using core::ChromaFormat;
    using core::Cicp;
    using core::CropRect;
    using core::ImageMeta;
    using core::MirrorAxis;
    using core::Transforms;

    namespace
    {

        ImageMeta baseMeta()
        {
            return ImageMeta{
                8, 6, 8, ChromaFormat::Yuv420, false, false, Cicp{1, 13, 6, false}, 1, false, {}, false, false, false};
        }

        TEST_CASE("describe names every chroma format and range")
        {
            auto meta = baseMeta();
            for (const auto &[chroma, text] :
                 std::array{std::pair{ChromaFormat::Yuv444, std::string_view{"YUV 4:4:4"}},
                            std::pair{ChromaFormat::Yuv422, std::string_view{"YUV 4:2:2"}},
                            std::pair{ChromaFormat::Yuv420, std::string_view{"YUV 4:2:0"}}}) {
                meta.chroma = chroma;
                meta.cicp.fullRange = false;
                CHECK(describe(meta).starts_with("8-bit " + std::string{text} + " (limited range)"));
                meta.cicp.fullRange = true;
                CHECK(describe(meta).starts_with("8-bit " + std::string{text} + " (full range)"));
            }

            meta.chroma = ChromaFormat::Yuv400;
            meta.cicp.fullRange = false;
            CHECK(describe(meta).starts_with("8-bit YUV 4:0:0 (monochrome)"));
            meta.cicp.fullRange = true;
            CHECK(describe(meta).starts_with("8-bit YUV 4:0:0 (monochrome)"));
        }

        TEST_CASE("describe labels known CICP triples and leaves unknown triples numeric")
        {
            auto meta = baseMeta();
            for (const auto &[cicp, expected] :
                 std::array{std::pair{Cicp{1, 13, 6, false}, std::string_view{"CICP 1/13/6 (sRGB)"}},
                            std::pair{Cicp{1, 1, 1, false}, std::string_view{"CICP 1/1/1 (BT.709)"}},
                            std::pair{Cicp{9, 16, 9, false}, std::string_view{"CICP 9/16/9 (BT.2020 PQ)"}},
                            std::pair{Cicp{9, 18, 9, false}, std::string_view{"CICP 9/18/9 (BT.2020 HLG)"}},
                            std::pair{Cicp{12, 16, 12, false}, std::string_view{"CICP 12/16/12 (P3 PQ)"}},
                            std::pair{Cicp{2, 2, 2, false}, std::string_view{"CICP 2/2/2 (unspecified)"}},
                            std::pair{Cicp{7, 8, 10, false}, std::string_view{"CICP 7/8/10"}}}) {
                meta.cicp = cicp;
                CHECK(describe(meta).find(expected) != std::string::npos);
            }
        }

        TEST_CASE("describe distinguishes absent straight and premultiplied alpha")
        {
            auto meta = baseMeta();
            meta.hasAlpha = false;
            meta.alphaPremultiplied = true;
            CHECK(describe(meta).find("alpha") == std::string::npos);

            meta.hasAlpha = true;
            meta.alphaPremultiplied = false;
            CHECK(describe(meta).ends_with("straight alpha"));

            meta.alphaPremultiplied = true;
            CHECK(describe(meta).ends_with("premultiplied alpha"));
        }

        TEST_CASE("describe omits frames for a still and reports an animation")
        {
            auto meta = baseMeta();
            CHECK(describe(meta).find("frame") == std::string::npos);

            meta.frameCount = 12;
            meta.animated = true;
            CHECK(describe(meta).ends_with("12 frames"));
        }

        TEST_CASE("describe reports each embedded metadata kind")
        {
            auto meta = baseMeta();
            meta.hasIcc = true;
            CHECK(describe(meta).ends_with("ICC"));

            meta = baseMeta();
            meta.hasExif = true;
            CHECK(describe(meta).ends_with("EXIF"));

            meta = baseMeta();
            meta.hasXmp = true;
            CHECK(describe(meta).ends_with("XMP"));

            meta = baseMeta();
            meta.hasExif = true;
            meta.exifOrientation = 6;
            CHECK(describe(meta).ends_with("EXIF, EXIF orientation 6"));

            meta.transforms.clap = CropRect{0, 0, 4, 3};
            CHECK_FALSE(describe(meta).contains("EXIF orientation"));
        }

        TEST_CASE("describe reports each transform and mirror axis")
        {
            auto meta = baseMeta();
            meta.transforms.clap = CropRect{0, 0, 4, 3};
            CHECK(describe(meta).ends_with("clap"));

            meta = baseMeta();
            meta.transforms.irotAngle = 2;
            CHECK(describe(meta).ends_with("irot 2"));

            meta = baseMeta();
            meta.transforms.imir = MirrorAxis::TopBottom;
            CHECK(describe(meta).ends_with("imir top-bottom"));

            meta.transforms.imir = MirrorAxis::LeftRight;
            CHECK(describe(meta).ends_with("imir left-right"));
        }

        TEST_CASE("describe creates the exact full combined string")
        {
            auto meta = baseMeta();
            meta.depth = 10;
            meta.chroma = ChromaFormat::Yuv444;
            meta.cicp = Cicp{9, 16, 9, true};
            meta.hasAlpha = true;
            meta.alphaPremultiplied = true;
            meta.frameCount = 12;
            meta.animated = true;
            meta.hasIcc = true;
            meta.hasExif = true;
            meta.exifOrientation = 6;
            meta.hasXmp = true;
            meta.transforms = Transforms{CropRect{1, 1, 4, 3}, 3, MirrorAxis::LeftRight};

            CHECK(describe(meta) == "10-bit YUV 4:4:4 (full range), CICP 9/16/9 (BT.2020 PQ), "
                                    "premultiplied alpha, "
                                    "12 frames, ICC, EXIF, XMP, clap, irot 3, imir left-right, "
                                    "→ sRGB (BT.2390 tone map from PQ 1000 nit)");
        }

        TEST_CASE("describe records HDR and wide-gamut presentation")
        {
            auto meta = baseMeta();
            for (const auto &[primaries, name] :
                 std::array{std::pair{std::uint16_t{4}, std::string_view{"BT.470M"}},
                            std::pair{std::uint16_t{5}, std::string_view{"BT.601-625"}},
                            std::pair{std::uint16_t{6}, std::string_view{"BT.601-625"}},
                            std::pair{std::uint16_t{7}, std::string_view{"SMPTE 240M/BT.601-525"}},
                            std::pair{std::uint16_t{9}, std::string_view{"Rec.2020"}},
                            std::pair{std::uint16_t{11}, std::string_view{"P3-DCI"}},
                            std::pair{std::uint16_t{12}, std::string_view{"P3-D65"}},
                            std::pair{std::uint16_t{22}, std::string_view{"EBU 3213-E"}}}) {
                meta.cicp = Cicp{primaries, 13, 6, false};
                CHECK(describe(meta).ends_with("→ sRGB (" + std::string{name} + " primaries)"));
            }

            meta.cicp = Cicp{11, 18, 12, false};
            meta.masteringPeakNits = 4'000.0F;
            CHECK(describe(meta).ends_with("→ sRGB (BT.2390 tone map from HLG 1000 nit)"));

            meta.cicp = Cicp{12, 16, 12, false};
            CHECK(describe(meta).ends_with("→ sRGB (BT.2390 tone map from PQ 4000 nit)"));
        }

        TEST_CASE("describe records safe fallbacks for unknown colour codes")
        {
            auto meta = baseMeta();
            meta.cicp.transfer = 99;
            CHECK(describe(meta).ends_with("→ sRGB (unknown transfer 99 treated as sRGB)"));

            meta = baseMeta();
            meta.cicp.primaries = 99;
            CHECK(describe(meta).ends_with("→ sRGB (unknown primaries 99 treated as BT.709)"));

            meta.cicp = Cicp{99, 99, 6, false};
            CHECK(describe(meta).ends_with("→ sRGB (unknown transfer 99 treated as sRGB; "
                                           "unknown primaries 99 treated as BT.709)"));
        }

        TEST_CASE("Describer names the format AVIF, the codec AV1 and describes the "
                  "metadata")
        {
            auto meta = baseMeta();
            meta.hasAlpha = true;
            const Describer describer;
            const core::IImageDescriber &describerInterface = describer;

            const auto description = describerInterface.describe(meta);

            CHECK(description.formatName == "AVIF");
            CHECK(description.compression == "AV1");
            CHECK(description.comments == describe(meta));
            CHECK(description.comments == "8-bit YUV 4:2:0 (limited range), CICP 1/13/6 (sRGB), straight alpha");
        }

    } // namespace
} // namespace pvdkit::avif
