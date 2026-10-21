#include "core/Describe.hpp"

namespace pvdkit::svg
{

    std::string describe(const core::ImageMeta &meta)
    {
        std::string result{"8-bit RGBA"};
        if (!meta.sourceDetail.empty()) {
            result += ", ";
            result += meta.sourceDetail;
        }
        return result;
    }

    core::ImageDescription Describer::describe(const core::ImageMeta &meta) const
    {
        return {"SVG", meta.compression, svg::describe(meta)};
    }

} // namespace pvdkit::svg
