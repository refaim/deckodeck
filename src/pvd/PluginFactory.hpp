#pragma once

#include <memory>

#include "pvd/Plugin.hpp"

namespace pvdkit::core
{
    struct DecoderOptions;
}

namespace pvdkit::pvd
{

    [[nodiscard]] std::unique_ptr<IPlugin> makePlugin(const core::DecoderOptions &options);
    [[nodiscard]] std::unique_ptr<IPlugin> makePlugin();

} // namespace pvdkit::pvd
