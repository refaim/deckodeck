#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "core/Format.hpp"

namespace pvdkit::svg
{
    namespace
    {

        std::vector<std::byte> bytes(const std::string_view text)
        {
            std::vector<std::byte> result;
            result.reserve(text.size());
            for (const char value : text) {
                result.push_back(static_cast<std::byte>(value));
            }
            return result;
        }

    } // namespace

    TEST_CASE("SVG recognition accepts a plain root, BOM, declaration, comments and case")
    {
        CHECK(recognises(bytes("<svg xmlns=\"http://www.w3.org/2000/svg\"/>")));
        CHECK(recognises(bytes("\xEF\xBB\xBF\r\n  <SVG viewBox=\"0 0 1 1\">")));
        CHECK(recognises(bytes("<?xml version=\"1.0\"?>\n<svg/>")));
        CHECK(recognises(bytes("<!-- generated -->\n<?xml version=\"1.0\"?><!-- again --><svg>")));
        CHECK(recognises(bytes("<svg>\n</svg>")));
    }

    TEST_CASE("SVG recognition accepts gzip magic and rejects unrelated or too-short input")
    {
        const auto gzip = bytes(std::string_view{"\x1F\x8B\x08\x00", 4});
        CHECK(isGzip(gzip));
        CHECK_FALSE(isGzip(bytes(std::string_view{"\x1F\x00", 2})));
        CHECK_FALSE(isGzip(bytes("\x1F")));
        CHECK(recognises(gzip));
        CHECK_FALSE(recognises(bytes("")));
        CHECK_FALSE(recognises(bytes("<")));
        CHECK_FALSE(recognises(bytes(" ")));
        CHECK_FALSE(recognises(bytes("x<")));
        CHECK_FALSE(recognises(bytes(std::string_view{"\x00<", 2})));
    }

    TEST_CASE("SVG recognition treats non-SVG markup as candidates for resvg to reject")
    {
        CHECK(recognises(bytes("<html><body/></html>")));
        CHECK(recognises(bytes("<svgx/>")));
        CHECK(recognises(bytes("<?xml version=\"1.0\"?>")));
        CHECK(recognises(bytes("<?xml version=\"1.0\"")));
        CHECK(recognises(bytes("<!-- unterminated")));
        CHECK(recognises(bytes("<svg")));
        CHECK(recognises(bytes("<!DOCTYPE svg PUBLIC \"x\" \"y\">")));
        CHECK(recognises(bytes("<s:svg xmlns:s=\"http://www.w3.org/2000/svg\"/>")));
    }

    TEST_CASE("SVG recognition accepts BOM and XML whitespace before a possibly remote root")
    {
        CHECK(recognises(bytes("\xEF\xBB\xBF<svg/>")));
        CHECK(recognises(bytes("\xEF\xBB\xBF")));
        CHECK(recognises(bytes("\xEF\xBB\xBF \r\n\t")));
        CHECK(recognises(bytes("  ")));
        CHECK(recognises(bytes("\r\n\t ")));
        CHECK(recognises(bytes("  <svg/>")));
        CHECK_FALSE(recognises(bytes("\xEF\xBB")));
        CHECK_FALSE(recognises(bytes(std::string_view{"\xEF\x00\x00", 3})));
        CHECK_FALSE(recognises(bytes(std::string_view{"\xEF\xBB\x00", 3})));
        CHECK_FALSE(recognises(bytes(" \xEF\xBB\xBF<svg/>")));
        CHECK_FALSE(recognises(bytes("\xFF\xFE<svg/>")));
        CHECK_FALSE(recognises(bytes("  x")));
        CHECK_FALSE(recognises(bytes("text")));
    }

    TEST_CASE("SVG recognition does not truncate a candidate at 4096 bytes")
    {
        std::string document(5000, ' ');
        document += "<!-- long prolog -->\n<svg xmlns=\"http://www.w3.org/2000/svg\"/>";
        CHECK(recognises(bytes(document)));
    }

} // namespace pvdkit::svg
