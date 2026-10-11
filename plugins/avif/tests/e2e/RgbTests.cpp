#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"
#include "RgbOutputChecks.hpp"

TEST_CASE("native RGB outputs match legacy AVIF colours through the wide entry point")
{
    pvdkit::e2e::checkNativeRgbOutputs(pvdkit::avif::tests::kAccepted);
}
