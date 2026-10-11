#include "core/Describe.hpp"

namespace pvdkit::rpgmvp
{
    namespace
    {

        std::string kind(const core::ImageMeta &meta)
        {
            if (meta.indexed) {
                return meta.hasAlpha ? "indexed+alpha" : "indexed";
            }
            if (meta.chroma == core::ChromaFormat::Yuv400) {
                return meta.hasAlpha ? "grey+alpha" : "grey";
            }
            return meta.hasAlpha ? "RGBA" : "RGB";
        }

    } // namespace

    std::string describe(const core::ImageMeta &meta)
    {
        std::string result = std::to_string(meta.depth) + "-bit " + kind(meta);
        if (meta.interlaced) {
            result += ", interlaced";
        }
        if (meta.hasIcc) {
            result += ", ICC";
        }
        return result;
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        return {"RPGMVP", "Deflate", rpgmvp::describe(meta)};
    }

} // namespace pvdkit::rpgmvp
