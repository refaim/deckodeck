#pragma once

#include <string>

#include "core/IDecoder.hpp"

namespace avifpvd::core {

[[nodiscard]] std::string describe(const ImageMeta &meta);

} // namespace avifpvd::core
