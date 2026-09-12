#pragma once

#include <string>

#include "core/IDecoder.hpp"

namespace pvdkit::core
{

    /// The host-facing words for one opened image: the fields of `pvd::ImageInfo` that depend on the
    /// format, as opposed to `pageCount` and `animated`, which `CodecPlugin` derives from `ImageMeta`.
    struct ImageDescription
    {
        std::string formatName, compression, comments;
    };

    /// Names a plugin's format and describes an image of it. Each plugin provides one implementation;
    /// `CodecPlugin` calls it exactly once per successful open, after the decoder parsed the file.
    class IImageDescriber
    {
      public:
        virtual ~IImageDescriber() = default;
        [[nodiscard]] virtual ImageDescription describe(const ImageMeta &meta) const = 0;
    };

} // namespace pvdkit::core
