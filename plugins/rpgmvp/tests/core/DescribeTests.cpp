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

        TEST_CASE("describe names palette greyscale and RGB source kinds with alpha distinctions")
        {
            for (const auto &[indexed, chroma, alpha, kind] : std::array{
                     std::tuple{true, core::ChromaFormat::Yuv444, false, std::string_view{"palette"}},
                     std::tuple{true, core::ChromaFormat::Yuv444, true, std::string_view{"palette with transparency"}},
                     std::tuple{false, core::ChromaFormat::Yuv400, false, std::string_view{"greyscale"}},
                     std::tuple{false, core::ChromaFormat::Yuv400, true, std::string_view{"greyscale with alpha"}},
                     std::tuple{false, core::ChromaFormat::Yuv444, false, std::string_view{"RGB"}},
                     std::tuple{false, core::ChromaFormat::Yuv444, true, std::string_view{"RGBA"}}}) {
                auto meta = baseMeta();
                meta.indexed = indexed;
                meta.chroma = chroma;
                meta.hasAlpha = alpha;
                CHECK(describe(meta) == "RPG Maker MV/MZ encrypted PNG, 8-bit " + std::string{kind});
            }
        }

        TEST_CASE("describe appends interlacing and uses the source depth")
        {
            auto meta = baseMeta();
            meta.depth = 16;
            meta.hasAlpha = true;
            meta.interlaced = true;
            CHECK(describe(meta) == "RPG Maker MV/MZ encrypted PNG, 16-bit RGBA, interlaced");
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
            CHECK(result.comments == "RPG Maker MV/MZ encrypted PNG, 4-bit palette with transparency");
        }

    } // namespace
} // namespace pvdkit::rpgmvp
