#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "core/colour/Pipeline.hpp"

namespace pvdkit::core::colour
{
    TEST_CASE("identical PQ presentations borrow one immutable gain table per plugin owner")
    {
        const auto tables = std::make_unique<const ColourTables>();
        const Presentation first{Cicp{9, 16, 9, false}, 1'000.0F, *tables};
        const Presentation second{Cicp{12, 16, 12, true}, 1'000.0F, *tables};
        REQUIRE(first.toneMapGains().size() == 65'536);
        CHECK(first.toneMapGains().data() == second.toneMapGains().data());
        std::array<std::uint16_t, 4> pixel{30'000, 20'000, 10'000, 12'345};
        auto other = pixel;
        first.apply(pixel);
        second.apply(other);
        CHECK(pixel[3] == 12'345);
        CHECK(other[3] == 12'345);
    }

    TEST_CASE("transfer tables share only exact transfer equivalence classes")
    {
        const auto tables = std::make_unique<const ColourTables>();
        const auto otherTables = std::make_unique<const ColourTables>();
        std::array<std::unique_ptr<Presentation>, 7> representatives;
        constexpr std::array<std::uint16_t, 7> transfers{1, 4, 5, 8, 16, 18, 13};
        for (std::size_t index = 0; index < transfers.size(); ++index) {
            representatives[index] =
                std::make_unique<Presentation>(Cicp{9, transfers[index], 9, false}, 1'000.0F, *tables);
            REQUIRE(representatives[index]->transferValues().size() == 65'536);
            const Presentation other{Cicp{9, transfers[index], 9, false}, 1'000.0F, *otherTables};
            CHECK(representatives[index]->transferValues().data() != other.transferValues().data());
            for (std::size_t previous = 0; previous < index; ++previous) {
                CHECK(representatives[index]->transferValues().data() !=
                      representatives[previous]->transferValues().data());
            }
        }
        for (const std::uint16_t transfer : {1, 6, 14, 15}) {
            const Presentation equivalent{Cicp{12, transfer, 0, true}, 400.0F, *tables};
            CHECK(equivalent.transferValues().data() == representatives[0]->transferValues().data());
        }
        for (const std::uint16_t transfer : {2, 13, 0, 255, 65535}) {
            const Presentation equivalent{Cicp{12, transfer, 0, true}, {}, *tables};
            CHECK(equivalent.transferValues().data() == representatives[6]->transferValues().data());
        }
        const Presentation inactive{Cicp{1, 13, 0, true}, {}, *tables};
        CHECK(inactive.transferValues().empty());
        CHECK(inactive.toneMapGains().empty());
    }

    TEST_CASE("PQ gain keys use exact normalized source peaks and keep plugin ownership separate")
    {
        const auto tables = std::make_unique<const ColourTables>();
        const auto otherTables = std::make_unique<const ColourTables>();
        const Presentation defaultPeak{Cicp{9, 16, 9, false}, {}, *tables};
        for (const float invalid :
             {0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            const Presentation same{Cicp{12, 16, 0, true}, invalid, *tables};
            CHECK(same.toneMapGains().data() == defaultPeak.toneMapGains().data());
        }
        const Presentation capped{Cicp{9, 16, 9, false}, 10'000.0F, *tables};
        const Presentation aboveCap{Cicp{9, 16, 9, false}, 20'000.0F, *tables};
        CHECK(capped.toneMapGains().data() == aboveCap.toneMapGains().data());
        const Presentation adjacent{Cicp{9, 16, 9, false}, std::nextafter(1'000.0F, 2'000.0F), *tables};
        CHECK(adjacent.toneMapGains().data() != defaultPeak.toneMapGains().data());
        CHECK(adjacent.sourcePeakNits() != defaultPeak.sourcePeakNits());
        const Presentation other{Cicp{9, 16, 9, false}, {}, *otherTables};
        CHECK(other.toneMapGains().data() != defaultPeak.toneMapGains().data());
    }

    TEST_CASE("a full PQ gain cache preserves borrowed tables and uses session-owned overflow")
    {
        const auto tables = std::make_unique<const ColourTables>();
        std::array<std::unique_ptr<Presentation>, 8> cached;
        for (std::size_t index = 0; index < cached.size(); ++index) {
            cached[index] =
                std::make_unique<Presentation>(Cicp{9, 16, 9, false}, 400.0F + static_cast<float>(index), *tables);
        }
        const Presentation overflow{Cicp{9, 16, 9, false}, 900.0F, *tables};
        {
            const Presentation another{Cicp{9, 16, 9, false}, 900.0F, *tables};
            CHECK(another.toneMapGains().data() != overflow.toneMapGains().data());
            CHECK(another.transferValues().data() == overflow.transferValues().data());
            CHECK(std::ranges::equal(another.toneMapGains(), overflow.toneMapGains()));
        }
        for (std::size_t index = 0; index < cached.size(); ++index) {
            const Presentation hit{Cicp{9, 16, 9, false}, 400.0F + static_cast<float>(index), *tables};
            CHECK(hit.toneMapGains().data() == cached[index]->toneMapGains().data());
        }
        auto values = std::array<std::uint16_t, 4>{10'000, 30'000, 60'000, 17};
        auto expected = values;
        const auto independent = std::make_unique<const ColourTables>();
        const Presentation reference{Cicp{9, 16, 9, false}, 900.0F, *independent};
        reference.apply(expected);
        overflow.apply(values);
        CHECK(values == expected);
    }

    TEST_CASE("concurrent first presentations publish one complete immutable table")
    {
        const auto tables = std::make_unique<const ColourTables>();
        std::array<std::unique_ptr<Presentation>, 16> sessions;
        {
            std::vector<std::jthread> workers;
            workers.reserve(sessions.size());
            for (auto &session : sessions) {
                workers.emplace_back([&session, &tables] {
                    session = std::make_unique<Presentation>(Cicp{9, 16, 9, false}, 1'200.0F, *tables);
                });
            }
        }
        for (const auto &session : sessions) {
            CHECK(session->transferValues().data() == sessions.front()->transferValues().data());
            CHECK(session->toneMapGains().data() == sessions.front()->toneMapGains().data());
            CHECK(session->transferValues().back() == 10'000.0F);
            CHECK(session->toneMapGains().front() == 0.0F);
            CHECK(session->toneMapGains().back() > 0.0F);
        }
    }

} // namespace pvdkit::core::colour
