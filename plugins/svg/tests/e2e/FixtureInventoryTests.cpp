#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"

namespace pvdkit::svg::tests
{
    namespace
    {

        namespace fs = std::filesystem;

        std::vector<std::string> discoveredFixtures()
        {
            std::vector<std::string> names;
            for (const auto &entry : fs::recursive_directory_iterator{PVDKIT_FIXTURE_DIR}) {
                if (entry.is_regular_file() && entry.path().extension() != ".md") {
                    names.push_back(fs::relative(entry.path(), PVDKIT_FIXTURE_DIR).generic_string());
                }
            }
            std::ranges::sort(names);
            return names;
        }

        void append(std::vector<std::string> &names, const auto &fixtures)
        {
            for (const auto &fixture : fixtures) {
                if constexpr (requires { fixture.name; }) {
                    names.emplace_back(fixture.name);
                } else {
                    names.emplace_back(fixture);
                }
            }
        }

    } // namespace

    TEST_CASE("SVG fixture inventory classifies every recursively discovered file exactly once")
    {
        CHECK(kAccepted.size() == 17);
        CHECK(kSupportAccepted.size() == 6);
        CHECK(kRejected.size() == 9);

        std::vector<std::string> expected;
        expected.reserve(kAccepted.size() + kSupportAccepted.size() + kRejected.size());
        append(expected, kAccepted);
        append(expected, kSupportAccepted);
        append(expected, kRejected);
        std::ranges::sort(expected);

        const auto duplicate = std::ranges::adjacent_find(expected);
        CHECK(duplicate == expected.end());
        CHECK(discoveredFixtures() == expected);
    }

} // namespace pvdkit::svg::tests
