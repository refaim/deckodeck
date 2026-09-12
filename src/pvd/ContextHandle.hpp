#pragma once

#include <memory>

#include "pvd/Plugin.hpp"

namespace avifpvd::pvd {

[[nodiscard]] void* toHost(std::unique_ptr<IFileSession> session) noexcept;
[[nodiscard]] std::unique_ptr<IFileSession> fromHost(void* context) noexcept;
[[nodiscard]] IFileSession* borrow(void* context) noexcept;

}  // namespace avifpvd::pvd
