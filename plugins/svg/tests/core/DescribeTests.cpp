#include <doctest/doctest.h>

#include "core/Describe.hpp"

namespace pvdkit::svg
{

    TEST_CASE("the SVG describer reports the still RGBA page and compression")
    {
        core::ImageMeta meta{};
        meta.compression = "GZip";
        meta.sourceDetail = "1024 x 768";

        const auto description = Describer{}.describe(meta);
        CHECK(description.formatName == "SVG");
        CHECK(description.compression == "GZip");
        CHECK(description.comments == "8-bit RGBA, 1024 x 768");
        CHECK(describe(core::ImageMeta{}).find("RGBA") != std::string::npos);
    }

} // namespace pvdkit::svg
