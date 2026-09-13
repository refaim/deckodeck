#include <doctest/doctest.h>

#include "PluginHost.hpp"

namespace pvdkit::e2e
{

    TEST_CASE("the host accepts supported decoded layouts with enough row pitch")
    {
        CHECK(isSupportedDecodeLayout(2, 24, 6));
        CHECK(isSupportedDecodeLayout(2, 32, 8));
        CHECK(isSupportedDecodeLayout(2, 64, 16));
        CHECK(isSupportedDecodeLayout(2, 64, 17));
    }

    TEST_CASE("the host rejects unsupported decoded layouts and undersized row pitch")
    {
        CHECK_FALSE(isSupportedDecodeLayout(2, 8, 2));
        CHECK_FALSE(isSupportedDecodeLayout(2, 24, 5));
        CHECK_FALSE(isSupportedDecodeLayout(2, 24, 7));
        CHECK_FALSE(isSupportedDecodeLayout(2, 32, 7));
        CHECK_FALSE(isSupportedDecodeLayout(2, 32, 9));
        CHECK_FALSE(isSupportedDecodeLayout(2, 64, 15));
        CHECK_FALSE(isSupportedDecodeLayout(2, 64, 0));
        CHECK_FALSE(isSupportedDecodeLayout(2, 64, -16));
    }

} // namespace pvdkit::e2e
