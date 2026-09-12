#pragma once

#include <memory>

#include "pvd/Plugin.hpp"

namespace avifpvd::pvd {

[[nodiscard]] std::unique_ptr<IPlugin> makePlugin();

}  // namespace avifpvd::pvd
