#include <array>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

#include "core/Describe.hpp"

namespace pvdkit::avif
{
    namespace
    {
        core::ImageMeta baseMeta()
        {
            core::ImageMeta meta{};
            meta.depth = 8;
            meta.chroma = core::ChromaFormat::Yuv420;
            meta.cicp = core::Cicp{1, 13, 6, false};
            meta.frameCount = 1;
            return meta;
        }

        TEST_CASE("describe keeps source depth and compact layout independent of range")
        {
            auto meta = baseMeta();
            for (const auto &[chroma, expected] :
                 std::array{std::pair{core::ChromaFormat::Yuv444, std::string_view{"8-bit 4:4:4, sRGB"}},
                            std::pair{core::ChromaFormat::Yuv422, std::string_view{"8-bit 4:2:2, sRGB"}},
                            std::pair{core::ChromaFormat::Yuv420, std::string_view{"8-bit 4:2:0, sRGB"}},
                            std::pair{core::ChromaFormat::Yuv400, std::string_view{"8-bit grey, sRGB"}}}) {
                meta.chroma = chroma;
                for (const bool fullRange : {false, true}) {
                    meta.cicp.fullRange = fullRange;
                    CHECK(describe(meta) == std::string{expected});
                    CHECK(describe(meta).size() <= 25);
                }
            }
            meta.depth = 12;
            CHECK(describe(meta) == "12-bit grey, sRGB");
        }

        TEST_CASE("describe keeps short colour and HDR presentation labels")
        {
            auto meta = baseMeta();
            for (const auto &[cicp, expected] :
                 std::array{std::pair{core::Cicp{1, 13, 6, false}, std::string_view{"8-bit 4:2:0, sRGB"}},
                            std::pair{core::Cicp{1, 1, 1, false}, std::string_view{"8-bit 4:2:0, BT.709"}},
                            std::pair{core::Cicp{1, 6, 6, false}, std::string_view{"8-bit 4:2:0, BT.709"}},
                            std::pair{core::Cicp{1, 14, 6, false}, std::string_view{"8-bit 4:2:0, BT.709"}},
                            std::pair{core::Cicp{1, 15, 6, false}, std::string_view{"8-bit 4:2:0, BT.709"}},
                            std::pair{core::Cicp{2, 2, 2, false}, std::string_view{"8-bit 4:2:0, unspecified"}},
                            std::pair{core::Cicp{9, 16, 9, false}, std::string_view{"8-bit 4:2:0, PQ → sRGB"}},
                            std::pair{core::Cicp{12, 16, 12, false}, std::string_view{"8-bit 4:2:0, PQ → sRGB"}},
                            std::pair{core::Cicp{9, 18, 9, false}, std::string_view{"8-bit 4:2:0, HLG → sRGB"}},
                            std::pair{core::Cicp{1, 4, 6, false}, std::string_view{"8-bit 4:2:0, gamma 2.2 → sRGB"}},
                            std::pair{core::Cicp{1, 5, 6, false}, std::string_view{"8-bit 4:2:0, gamma 2.8 → sRGB"}},
                            std::pair{core::Cicp{1, 8, 0, false}, std::string_view{"8-bit 4:2:0, linear → sRGB"}}}) {
                meta.cicp = cicp;
                CHECK(describe(meta) == std::string{expected});
                CHECK(describe(meta).size() <= 45);
            }
        }

        TEST_CASE("describe keeps converted source gamut concise")
        {
            auto meta = baseMeta();
            for (const auto &[primaries, expected] :
                 std::array{std::pair{std::uint16_t{4}, std::string_view{"8-bit 4:2:0, BT.470M → sRGB"}},
                            std::pair{std::uint16_t{5}, std::string_view{"8-bit 4:2:0, BT.601 → sRGB"}},
                            std::pair{std::uint16_t{6}, std::string_view{"8-bit 4:2:0, BT.601 → sRGB"}},
                            std::pair{std::uint16_t{7}, std::string_view{"8-bit 4:2:0, SMPTE 240M → sRGB"}},
                            std::pair{std::uint16_t{9}, std::string_view{"8-bit 4:2:0, Rec.2020 → sRGB"}},
                            std::pair{std::uint16_t{11}, std::string_view{"8-bit 4:2:0, P3-DCI → sRGB"}},
                            std::pair{std::uint16_t{12}, std::string_view{"8-bit 4:2:0, P3-D65 → sRGB"}},
                            std::pair{std::uint16_t{22}, std::string_view{"8-bit 4:2:0, EBU 3213 → sRGB"}}}) {
                meta.cicp.primaries = primaries;
                CHECK(describe(meta) == std::string{expected});
                CHECK(describe(meta).size() <= 45);
            }
        }

        TEST_CASE("describe flags unknown colour fallbacks without numeric prose")
        {
            auto meta = baseMeta();
            for (const auto &[cicp, expected] : std::array{
                     std::pair{core::Cicp{1, 99, 6, false}, std::string_view{"8-bit 4:2:0, sRGB (fallback)"}},
                     std::pair{core::Cicp{99, 13, 6, false}, std::string_view{"8-bit 4:2:0, sRGB (fallback)"}},
                     std::pair{core::Cicp{99, 99, 6, false}, std::string_view{"8-bit 4:2:0, sRGB (fallback)"}},
                     std::pair{core::Cicp{9, 99, 6, false},
                               std::string_view{"8-bit 4:2:0, Rec.2020 → sRGB (fallback)"}},
                     std::pair{core::Cicp{99, 16, 6, false}, std::string_view{"8-bit 4:2:0, PQ → sRGB (fallback)"}}}) {
                meta.cicp = cicp;
                CHECK(describe(meta) == std::string{expected});
                CHECK(describe(meta).size() <= 45);
            }
        }

        TEST_CASE("describe keeps source alpha concise and ignores unused premultiplication")
        {
            auto meta = baseMeta();
            meta.alphaPremultiplied = true;
            CHECK(describe(meta) == "8-bit 4:2:0, sRGB");
            meta.hasAlpha = true;
            meta.alphaPremultiplied = false;
            CHECK(describe(meta) == "8-bit 4:2:0, alpha, sRGB");
            meta.alphaPremultiplied = true;
            CHECK(describe(meta) == "8-bit 4:2:0, premul alpha, sRGB");
        }

        TEST_CASE("describe suppresses ancillary metadata transforms animation and HDR algorithm details")
        {
            auto meta = baseMeta();
            meta.depth = 10;
            meta.cicp = core::Cicp{9, 16, 9, true};
            meta.hasAlpha = true;
            meta.alphaPremultiplied = true;
            meta.animated = true;
            meta.frameCount = 12;
            meta.hasIcc = true;
            meta.hasExif = true;
            meta.hasXmp = true;
            meta.exifOrientation = 6;
            meta.masteringPeakNits = 4000.0F;
            meta.transforms = core::Transforms{core::CropRect{1, 1, 4, 3}, 3, core::MirrorAxis::LeftRight};
            const auto text = describe(meta);
            CHECK(text == "10-bit 4:2:0, premul alpha, PQ → sRGB");
            CHECK(text.size() <= 45);
            for (const auto redundant :
                 {"YUV", "range", "CICP", "ICC", "EXIF", "XMP", "clap", "irot", "imir", "frames", "nit", "BT.2390"}) {
                CHECK_FALSE(text.contains(redundant));
            }
            meta.transforms.imir = core::MirrorAxis::TopBottom;
            meta.exifOrientation = 9;
            CHECK(describe(meta) == text);
        }

        TEST_CASE("Describer retains AVIF format and AV1 compression with compact comments")
        {
            auto meta = baseMeta();
            meta.hasAlpha = true;
            const Describer describer;
            const core::IImageDescriber &interface = describer;
            const auto result = interface.describe(meta);
            CHECK(result.formatName == "AVIF");
            CHECK(result.compression == "AV1");
            CHECK(result.comments == "8-bit 4:2:0, alpha, sRGB");
        }
    } // namespace
} // namespace pvdkit::avif
