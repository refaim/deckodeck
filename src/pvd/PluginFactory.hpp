#pragma once

#include <memory>

#include "pvd/Plugin.hpp"

namespace pvdkit::pvd {

[[nodiscard]] std::unique_ptr<IPlugin> makePlugin();

}  // namespace pvdkit::pvd
