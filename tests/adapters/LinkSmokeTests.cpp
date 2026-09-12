#include <ostream>
#include <string_view>

#include <avif/avif.h>
#include <doctest/doctest.h>

TEST_CASE("libavif links statically with the dav1d decoder") {
  CHECK(std::string_view{avifVersion()}.empty() == false);

  const auto codecName = avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_DECODE);
  REQUIRE(codecName != nullptr);
  CHECK(std::string_view{codecName} == "dav1d");
}
