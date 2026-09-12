#include <array>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

#include "core/Describe.hpp"

namespace avifpvd::core {
namespace {

ImageMeta baseMeta() {
  return ImageMeta{8,
                   6,
                   8,
                   ChromaFormat::Yuv420,
                   false,
                   false,
                   Cicp{1, 13, 6, false},
                   1,
                   false,
                   {},
                   false,
                   false,
                   false};
}

TEST_CASE("describe names every chroma format and range") {
  auto meta = baseMeta();
  for (const auto &[chroma, text] : std::array{
           std::pair{ChromaFormat::Yuv444, std::string_view{"YUV 4:4:4"}},
           std::pair{ChromaFormat::Yuv422, std::string_view{"YUV 4:2:2"}},
           std::pair{ChromaFormat::Yuv420, std::string_view{"YUV 4:2:0"}}}) {
    meta.chroma = chroma;
    meta.cicp.fullRange = false;
    CHECK(describe(meta).starts_with("8-bit " + std::string{text} +
                                     " (limited range)"));
    meta.cicp.fullRange = true;
    CHECK(describe(meta).starts_with("8-bit " + std::string{text} +
                                     " (full range)"));
  }

  meta.chroma = ChromaFormat::Yuv400;
  meta.cicp.fullRange = false;
  CHECK(describe(meta).starts_with("8-bit YUV 4:0:0 (monochrome)"));
  meta.cicp.fullRange = true;
  CHECK(describe(meta).starts_with("8-bit YUV 4:0:0 (monochrome)"));
}

TEST_CASE(
    "describe labels known CICP triples and leaves unknown triples numeric") {
  auto meta = baseMeta();
  for (const auto &[cicp, expected] : std::array{
           std::pair{Cicp{1, 13, 6, false},
                     std::string_view{"CICP 1/13/6 (sRGB)"}},
           std::pair{Cicp{1, 1, 1, false},
                     std::string_view{"CICP 1/1/1 (BT.709)"}},
           std::pair{Cicp{9, 16, 9, false},
                     std::string_view{"CICP 9/16/9 (BT.2020 PQ)"}},
           std::pair{Cicp{9, 18, 9, false},
                     std::string_view{"CICP 9/18/9 (BT.2020 HLG)"}},
           std::pair{Cicp{12, 16, 12, false},
                     std::string_view{"CICP 12/16/12 (P3 PQ)"}},
           std::pair{Cicp{2, 2, 2, false},
                     std::string_view{"CICP 2/2/2 (unspecified)"}},
           std::pair{Cicp{7, 8, 10, false}, std::string_view{"CICP 7/8/10"}}}) {
    meta.cicp = cicp;
    CHECK(describe(meta).ends_with(expected));
  }
}

TEST_CASE("describe distinguishes absent straight and premultiplied alpha") {
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

TEST_CASE("describe omits frames for a still and reports an animation") {
  auto meta = baseMeta();
  CHECK(describe(meta).find("frame") == std::string::npos);

  meta.frameCount = 12;
  meta.animated = true;
  CHECK(describe(meta).ends_with("12 frames"));
}

TEST_CASE("describe reports each embedded metadata kind") {
  auto meta = baseMeta();
  meta.hasIcc = true;
  CHECK(describe(meta).ends_with("ICC"));

  meta = baseMeta();
  meta.hasExif = true;
  CHECK(describe(meta).ends_with("EXIF"));

  meta = baseMeta();
  meta.hasXmp = true;
  CHECK(describe(meta).ends_with("XMP"));
}

TEST_CASE("describe reports each transform and mirror axis") {
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

TEST_CASE("describe creates the exact full combined string") {
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
  meta.hasXmp = true;
  meta.transforms = Transforms{CropRect{1, 1, 4, 3}, 3, MirrorAxis::LeftRight};

  CHECK(describe(meta) ==
        "10-bit YUV 4:4:4 (full range), CICP 9/16/9 (BT.2020 PQ), "
        "premultiplied alpha, "
        "12 frames, ICC, EXIF, XMP, clap, irot 3, imir left-right");
}

} // namespace
} // namespace avifpvd::core
