#include <cstdint>
#include <new>
#include <stdexcept>

#include <doctest/doctest.h>

#include "pvd/Firewall.hpp"

#include "pvd/PvdApi.hpp"

namespace pvdkit::pvd
{
    namespace
    {

        enum class Failure : std::uint8_t
        {
            None,
            BadAlloc,
            RuntimeError,
            Integer
        };

        struct IntOperation
        {
            Failure failure;

            int operator()() const
            {
                switch (failure) {
                case Failure::None:
                    return 42;
                case Failure::BadAlloc:
                    throw std::bad_alloc{};
                case Failure::RuntimeError:
                    throw std::runtime_error{"failure"};
                case Failure::Integer:
                    throw 7;
                }
                return 0;
            }
        };

        TEST_CASE("guarded returns values and catches every exception category")
        {
            CHECK(noexcept(guarded(IntOperation{Failure::None}, -1)));
            CHECK(guarded(IntOperation{Failure::None}, -1) == 42);
            CHECK(guarded(IntOperation{Failure::BadAlloc}, -1) == -1);
            CHECK(guarded(IntOperation{Failure::RuntimeError}, -2) == -2);
            CHECK(guarded(IntOperation{Failure::Integer}, -3) == -3);
        }

        TEST_CASE("guarded void overload runs or swallows")
        {
            bool called = false;
            CHECK(noexcept(guarded([&] { called = true; })));
            guarded([&] { called = true; });
            CHECK(called);
            CHECK_NOTHROW(guarded([] { throw std::runtime_error{"ignored"}; }));
            CHECK_NOTHROW(guarded([] { throw std::bad_alloc{}; }));
            CHECK_NOTHROW(guarded([] { throw 9; }));
        }

        TEST_CASE("guarded preserves PVD scalar return types")
        {
            const auto boolResult = guarded([]() -> BOOL { return BOOL{1}; }, BOOL{0});
            const auto versionResult = guarded([]() -> UINT32 { return UINT32{17}; }, UINT32{0});

            static_assert(std::is_same_v<decltype(boolResult), const BOOL>);
            static_assert(std::is_same_v<decltype(versionResult), const UINT32>);
            CHECK(boolResult == BOOL{1});
            CHECK(versionResult == UINT32{17});
        }

    } // namespace
} // namespace pvdkit::pvd
