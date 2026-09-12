#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace pvdkit::pvd
{

    template <class F>
        requires(!std::is_void_v<std::invoke_result_t<F &&>>)
    [[nodiscard]] auto guarded(F &&function, std::invoke_result_t<F &&> fallback) noexcept -> std::invoke_result_t<F &&>
    {
        try {
            return std::invoke(std::forward<F>(function));
        } catch (...) {
            return fallback;
        }
    }

    /// The void overload runs through the non-void one, so the one `catch (...)` above is the whole
    /// firewall.
    template <class F>
        requires std::is_void_v<std::invoke_result_t<F &&>>
    void guarded(F &&function) noexcept
    {
        static_cast<void>(guarded(
            [&function] {
                std::invoke(std::forward<F>(function));
                return true;
            },
            false));
    }

} // namespace pvdkit::pvd
