#include "pvd/ContextHandle.hpp"

#include <memory>
#include <utility>

namespace avifpvd::pvd {

void* toHost(std::unique_ptr<IFileSession> session) noexcept { return session.release(); }

std::unique_ptr<IFileSession> fromHost(void* context) noexcept {
  return std::unique_ptr<IFileSession>{static_cast<IFileSession*>(context)};
}

IFileSession* borrow(void* context) noexcept { return static_cast<IFileSession*>(context); }

}  // namespace avifpvd::pvd
