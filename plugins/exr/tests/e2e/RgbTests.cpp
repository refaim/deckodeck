#include <doctest/doctest.h>

#include "FixtureExpectations.hpp"
#include "RgbOutputChecks.hpp"

TEST_CASE("native RGB outputs preserve every accepted EXR image")
{
    // The wide ABI must opt in to RGB48/RGBA64 for disk and virtual files. The
    // helper compares every retained colour and alpha byte with the legacy ABI.
    pvdkit::e2e::checkNativeRgbOutputs(pvdkit::exr::tests::kAccepted);
}

TEST_CASE("EXR page navigation retains outstanding host buffers and selects native alpha per layer")
{
    using namespace pvdkit::e2e;
    const auto plugin = loadInitializedPlugin();
    const auto &exports = plugin.exports();
    const auto file = readFixture("layer_rgb.exr");
    for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
        rgbChecks::Session legacy{exports, file, mode, false};
        rgbChecks::Session native{exports, file, mode, true};
        REQUIRE(legacy.image().info.nPages == 2);
        REQUIRE(native.image().info.nPages == 2);
        auto firstLegacy = rgbChecks::decodeRaw(exports, legacy.image().context, 0);
        auto firstNative = rgbChecks::decodeRaw(exports, native.image().context, 0);
        REQUIRE(firstNative.decode.nBPP == 65);
        CHECK(firstNative.page.nBPP == 64);
        CHECK(firstNative.decode.Flags == PVD_IDF_ALPHA);
        rgbChecks::checkNativePage(firstNative, firstLegacy);
        const auto retained = firstNative.pixels();
        auto secondLegacy = rgbChecks::decodeRaw(exports, legacy.image().context, 1);
        auto secondNative = rgbChecks::decodeRaw(exports, native.image().context, 1);
        REQUIRE(secondNative.decode.nBPP == 49);
        CHECK(secondNative.page.nBPP == 48);
        CHECK(secondNative.decode.Flags == 0);
        CHECK(secondNative.page.lFrameTime == 0);
        rgbChecks::checkNativePage(secondNative, secondLegacy);
        CHECK(firstNative.pixels() == retained);
        auto revisited = rgbChecks::decodeRaw(exports, native.image().context, 0);
        CHECK(revisited.pixels() == retained);
        CHECK(firstNative.pixels() == retained);
        exports.pageFree(native.image().context, &revisited.decode);
        exports.pageFree(native.image().context, &firstNative.decode);
        exports.pageFree(native.image().context, &secondNative.decode);
        exports.pageFree(legacy.image().context, &firstLegacy.decode);
        exports.pageFree(legacy.image().context, &secondLegacy.decode);
    }
    exports.exit();
}
