#include <string>

#include <doctest/doctest.h>

#include "PluginHost.hpp"
#include "pvd/PvdApi.hpp"

namespace pvdkit::e2e
{
    namespace
    {

        std::string text(const char *value)
        {
            return value == nullptr ? std::string{"<null>"} : std::string{value};
        }

    } // namespace

    TEST_CASE("SVG.pvd identifies itself through the nine-export host boundary")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);

        pvdInfoPlugin info{};
        exports.pluginInfo(&info);
        CHECK(info.Priority == 10);
        CHECK(text(info.pName) == "SVG");
        CHECK(text(info.pVersion) == "1.0.0");
        CHECK(text(info.pComments).find("resvg 0.48.1") != std::string::npos);
        exports.exit();
    }

    TEST_CASE("SVG.pvd rejects malformed input in disk and memory modes")
    {
        const auto plugin = loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto file = readFixture("malformed.svg");
        CHECK_FALSE(openImage(exports, file, OpenMode::Disk).has_value());
        CHECK_FALSE(openImage(exports, file, OpenMode::Memory).has_value());
        exports.exit();
    }

    TEST_CASE("SVG.pvd survives repeated load, init, exit and unload cycles")
    {
        for (int cycle = 0; cycle < 3; ++cycle) {
            const auto plugin = loadInitializedPlugin();
            const auto &exports = plugin.exports();

            pvdInfoPlugin info{};
            exports.pluginInfo(&info);
            CHECK(info.Priority == 10);
            CHECK(text(info.pName) == "SVG");
            CHECK(text(info.pVersion) == "1.0.0");
            exports.exit();
        }
    }

} // namespace pvdkit::e2e
