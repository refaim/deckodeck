#pragma once

#include <string>

#include "core/IDecoder.hpp"
#include "core/IImageDescriber.hpp"

namespace pvdkit::avif {

/// The comments line PictureView shows for an AVIF: depth, chroma and range, CICP triple with its
/// well-known name, alpha kind, frame count, embedded metadata and transformative properties.
[[nodiscard]] std::string describe(const core::ImageMeta &meta);

/// The AVIF `IImageDescriber`: format "AVIF", compression "AV1", comments from `describe`.
class Describer final : public core::IImageDescriber {
public:
  [[nodiscard]] core::ImageDescription
  describe(const core::ImageMeta &meta) const override;
};

} // namespace pvdkit::avif
