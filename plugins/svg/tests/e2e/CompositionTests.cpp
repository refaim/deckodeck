#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "pvd/PluginConstants.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/PvdApi.hpp"
#include "pvd/Shim.hpp"

namespace pvdkit::svg::tests
{
    namespace
    {

        std::vector<std::byte> fixture(const std::string_view name)
        {
            const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
            std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
            std::ifstream stream{path, std::ios::binary};
            REQUIRE(stream.good());
            stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
            return bytes;
        }

    } // namespace

    TEST_CASE("the default SVG composition exposes one RGBA page through the PVD shim")
    {
        const auto plugin = pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        pvd::Shim shim{*plugin, pvd::kPluginIdentity};
        const auto bytes = fixture("basic-2x2-colors.svg");

        pvdInfoImage image{};
        void *context = nullptr;
        REQUIRE(shim.fileOpen("memory.svg", 0, reinterpret_cast<const BYTE *>(bytes.data()),
                              static_cast<UINT32>(bytes.size()), &image, &context) == TRUE);
        REQUIRE(context != nullptr);
        CHECK(image.nPages == 1);
        CHECK(image.Flags == 0);

        pvdInfoPage page{};
        REQUIRE(shim.pageInfo(context, 0, &page) == TRUE);
        CHECK(page.lWidth == 2);
        CHECK(page.lHeight == 2);

        pvdInfoDecode decoded{};
        REQUIRE(shim.pageDecode(context, 0, &decoded, nullptr, nullptr) == TRUE);
        CHECK(decoded.nBPP == 32);
        CHECK(decoded.lImagePitch == 8);
        CHECK(decoded.Flags == PVD_IDF_ALPHA);

        shim.pageFree(context, &decoded);
        shim.fileClose(context);
    }

} // namespace pvdkit::svg::tests
