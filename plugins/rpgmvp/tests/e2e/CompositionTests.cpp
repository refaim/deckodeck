#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "core/IDecoder.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/PvdApi.hpp"
#include "pvd/Shim.hpp"

namespace pvdkit::rpgmvp::tests
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

    TEST_CASE("the default RPGMVP composition exposes BGRA64 through the PVD shim")
    {
        struct Case
        {
            std::string_view name;
            std::uint32_t width;
            UINT32 flags;
        };
        constexpr Case cases[]{
            {"rgba16_60x20_par.rpgmvp", 60, PVD_IDF_ALPHA},
            {"rgb16_88x4a.rpgmvp", 88, 0},
        };
        const auto plugin = pvd::makePlugin();
        REQUIRE(plugin != nullptr);
        pvd::Shim shim{*plugin, pvd::kPluginIdentity};

        for (const auto &item : cases) {
            CAPTURE(item.name);
            const auto bytes = fixture(item.name);
            pvdInfoImage image{};
            void *context = nullptr;
            REQUIRE(shim.fileOpen("memory.rpgmvp", 0, reinterpret_cast<const BYTE *>(bytes.data()),
                                  static_cast<UINT32>(bytes.size()), &image, &context) == TRUE);
            REQUIRE(context != nullptr);

            pvdInfoPage page{};
            REQUIRE(shim.pageInfo(context, 0, &page) == TRUE);
            CHECK(page.lWidth == item.width);
            pvdInfoDecodeEx extended{};
            auto &decoded = *reinterpret_cast<pvdInfoDecode *>(&extended);
            REQUIRE(shim.pageDecode(context, 0, &decoded, nullptr, nullptr) == TRUE);
            CHECK(decoded.nBPP == 64);
            CHECK(decoded.lImagePitch == static_cast<INT32>(item.width * 8U));
            const auto iccFlags = pvd::detail::iccExperimentEnabled() ? UINT32{PVD_IDF_ICC_PROFILE} : UINT32{0};
            CHECK(decoded.Flags == (item.flags | iccFlags));
            CHECK((extended.pIccProfile != nullptr) == pvd::detail::iccExperimentEnabled());
            CHECK((extended.cbIccProfile != 0) == pvd::detail::iccExperimentEnabled());

            shim.pageFree(context, &decoded);
            shim.fileClose(context);
        }
    }

} // namespace pvdkit::rpgmvp::tests
