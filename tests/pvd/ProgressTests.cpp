#include <cstdint>

#include <doctest/doctest.h>

#include "pvd/Types.hpp"

namespace pvdkit::pvd
{
    namespace
    {

        TEST_CASE("progress without a callback always continues")
        {
            const Progress progress;
            CHECK(progress.report(2, 3));
        }

        TEST_CASE("progress forwards arguments and returns the callback result")
        {
            std::uint32_t observedStep = 0;
            std::uint32_t observedSteps = 0;
            bool callbackResult = true;
            const Progress progress{[&](const std::uint32_t step, const std::uint32_t steps) {
                observedStep = step;
                observedSteps = steps;
                return callbackResult;
            }};

            CHECK(progress.report(1, 4));
            CHECK(observedStep == 1);
            CHECK(observedSteps == 4);

            callbackResult = false;
            CHECK_FALSE(progress.report(3, 4));
            CHECK(observedStep == 3);
            CHECK(observedSteps == 4);
        }

    } // namespace
} // namespace pvdkit::pvd
