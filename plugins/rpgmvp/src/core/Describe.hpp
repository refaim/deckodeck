#pragma once

#include <string>

#include "core/IDecoder.hpp"
#include "core/IImageDescriber.hpp"

namespace pvdkit::rpgmvp
{

    [[nodiscard]] std::string describe(const core::ImageMeta &meta);

    class Describer final : public core::IImageDescriber
    {
      public:
        [[nodiscard]] core::ImageDescription describe(const core::ImageMeta &meta) const override;
    };

} // namespace pvdkit::rpgmvp
