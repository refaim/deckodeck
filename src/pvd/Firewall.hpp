#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace avifpvd::pvd {

template <class F>
  requires(!std::is_void_v<std::invoke_result_t<F&&>>)
[[nodiscard]] auto guarded(F&& function, std::invoke_result_t<F&&> fallback) noexcept
    -> std::invoke_result_t<F&&> {
  try {
    return std::invoke(std::forward<F>(function));
  } catch (...) {
    return fallback;
  }
}

template <class F>
  requires std::is_void_v<std::invoke_result_t<F&&>>
void guarded(F&& function) noexcept {
  try {
    std::invoke(std::forward<F>(function));
  } catch (...) {
  }
}

}  // namespace avifpvd::pvd
