#pragma once

#include <doctest/doctest.h>

namespace pvdkit::sequence::test
{

    // Keep doctest's control-flow machinery in one helper so focused coverage measures the
    // sequence checks and driver branches, not the framework's assertion failure branches.
    inline void check(const bool condition)
    {
        CHECK(condition);
    }

    inline void require(const bool condition)
    {
        REQUIRE(condition);
    }

} // namespace pvdkit::sequence::test
