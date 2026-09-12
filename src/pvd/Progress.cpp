#include "pvd/Types.hpp"

#include <utility>

namespace pvdkit::pvd
{

    Progress::Progress(Fn fn) : fn_{std::move(fn)}
    {
    }

    bool Progress::report(const std::uint32_t step, const std::uint32_t steps) const
    {
        return !fn_ || fn_(step, steps);
    }

} // namespace pvdkit::pvd
