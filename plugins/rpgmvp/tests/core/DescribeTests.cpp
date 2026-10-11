#include <array>
#include <string_view>
#include <tuple>
#include <utility>

#include <doctest/doctest.h>

#include "core/Describe.hpp"

namespace pvdkit::rpgmvp
{
    namespace
    {

        core::ImageMeta baseMeta()
        {
            core::ImageMeta meta{};
            meta.width = 12;
            meta.height = 8;
            meta.depth = 8;
            meta.chroma = core::ChromaFormat::Yuv444;
            meta.frameCount = 1;
            return meta;
        }

        TEST_CASE("describe names indexed grey and RGB source kinds with alpha distinctions")
        {
            for (const auto &[indexed, chroma, alpha, kind] :
                 std::array{std::tuple{true, core::ChromaFormat::Yuv444, false, std::string_view{"indexed"}},
                            std::tuple{true, core::ChromaFormat::Yuv444, true, std::string_view{"indexed+alpha"}},
                            std::tuple{false, core::ChromaFormat::Yuv400, false, std::string_view{"grey"}},
                            std::tuple{false, core::ChromaFormat::Yuv400, true, std::string_view{"grey+alpha"}},
                            std::tuple{false, core::ChromaFormat::Yuv444, false, std::string_view{"RGB"}},
                            std::tuple{false, core::ChromaFormat::Yuv444, true, std::string_view{"RGBA"}}}) {
                auto meta = baseMeta();
                meta.indexed = indexed;
                meta.chroma = chroma;
                meta.hasAlpha = alpha;
                CHECK(describe(meta) == "8-bit " + std::string{kind});
                CHECK(describe(meta).size() <= 25);
                CHECK_FALSE(describe(meta).contains("RPG Maker"));
                CHECK_FALSE(describe(meta).contains("PNG"));
            }
        }

        TEST_CASE("describe appends interlacing and uses the source depth")
        {
            auto meta = baseMeta();
            meta.depth = 16;
            meta.hasAlpha = true;
            meta.interlaced = true;
            CHECK(describe(meta) == "16-bit RGBA, interlaced");
        }

        TEST_CASE("describe reports an embedded ICC profile and omits it otherwise")
        {
            auto meta = baseMeta();
            CHECK_FALSE(describe(meta).contains("ICC"));

            meta.hasIcc = true;
            CHECK(describe(meta).ends_with(", ICC"));

            meta.interlaced = true;
            CHECK(describe(meta).ends_with("interlaced, ICC"));
        }

        TEST_CASE("Describer names the RPGMVP format and Deflate compression")
        {
            auto meta = baseMeta();
            meta.indexed = true;
            meta.depth = 4;
            meta.hasAlpha = true;
            const Describer describer;
            const core::IImageDescriber &interface = describer;

            const auto result = interface.describe(meta);

            CHECK(result.formatName == "RPGMVP");
            CHECK(result.compression == "Deflate");
            CHECK(result.comments == "4-bit indexed+alpha");
        }

    } // namespace
} // namespace pvdkit::rpgmvp
