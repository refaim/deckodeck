#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"
#include "RgbOutputChecks.hpp"

TEST_CASE("native RGB outputs preserve every accepted RPGMVP image")
{
    pvdkit::e2e::checkNativeRgbOutputs(pvdkit::rpgmvp::tests::kAccepted);
}
