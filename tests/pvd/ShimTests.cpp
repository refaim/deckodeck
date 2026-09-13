#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "pvd/ContextHandle.hpp"
#include "pvd/Shim.hpp"

#include "Fakes.hpp"
#include "pvd/PvdApi.hpp"

namespace pvdkit::pvd
{
    namespace
    {

        // The identity Exports.cpp would hand to a real Shim comes from the plugin's generated
        // pvd/PluginConstants.hpp; the shim itself only forwards whatever it is given.
        constexpr PluginIdentity kIdentity{42, "Fake", "9.8.7"};

        struct CallbackObservation
        {
            std::uint32_t step = 0;
            std::uint32_t steps = 0;
            BOOL result = TRUE;
        };

        BOOL __stdcall observeProgress(void *context, const UINT32 step, const UINT32 steps)
        {
            auto &observation = *static_cast<CallbackObservation *>(context);
            observation.step = step;
            observation.steps = steps;
            return observation.result;
        }

        void openSession(Shim &shim, pvdInfoImage &imageInfo, void *&context)
        {
            constexpr std::array head{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
            REQUIRE(shim.fileOpen("sample.avif", 123, reinterpret_cast<const BYTE *>(head.data()),
                                  static_cast<UINT32>(head.size()), &imageInfo, &context) == TRUE);
        }

        TEST_CASE("shim marshals every successful operation and preserves session storage")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};

            CHECK(shim.init() == PVD_CURRENT_INTERFACE_VERSION);
            CHECK_NOTHROW(shim.exit());

            pvdInfoPlugin pluginInfo{};
            shim.pluginInfo(&pluginInfo);
            CHECK(pluginInfo.Priority == state.pluginInfo.priority);
            CHECK(pluginInfo.pName == state.pluginInfo.name.c_str());
            CHECK(pluginInfo.pVersion == state.pluginInfo.version.c_str());
            CHECK(pluginInfo.pComments == state.pluginInfo.comments.c_str());

            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);
            REQUIRE(context != nullptr);
            CHECK(state.openedName == "sample.avif");
            CHECK(state.openedFileSize == 123);
            CHECK(state.openedHead == std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}});

            const auto &sessionInfo = borrow(context)->imageInfo();
            CHECK(imageInfo.nPages == sessionInfo.pageCount);
            CHECK(imageInfo.Flags == PVD_IIF_ANIMATED);
            CHECK(imageInfo.pFormatName == sessionInfo.formatName.c_str());
            CHECK(imageInfo.pCompression == sessionInfo.compression.c_str());
            CHECK(imageInfo.pComments == sessionInfo.comments.c_str());

            pvdInfoPage pageInfo{};
            CHECK(shim.pageInfo(context, 1, &pageInfo) == TRUE);
            CHECK(state.observedPage == 1);
            CHECK(pageInfo.lWidth == state.pageInfo.width);
            CHECK(pageInfo.lHeight == state.pageInfo.height);
            CHECK(pageInfo.nBPP == state.pageInfo.bitsPerPixel);
            CHECK(pageInfo.lFrameTime == state.pageInfo.frameTimeMs);

            pvdInfoDecode decodeInfo{};
            CHECK(shim.pageDecode(context, 1, &decodeInfo, nullptr, nullptr) == TRUE);
            CHECK(state.progressResult);
            CHECK(decodeInfo.pImage != nullptr);
            CHECK(decodeInfo.pPalette == nullptr);
            CHECK(decodeInfo.Flags == 0);
            CHECK(decodeInfo.nBPP == 32);
            CHECK(decodeInfo.nColorsUsed == 0);
            CHECK(decodeInfo.lImagePitch == 8);

            shim.pageFree(context, &decodeInfo);
            CHECK(state.freeCalls == 1);
            CHECK(state.freedData == reinterpret_cast<const std::byte *>(decodeInfo.pImage));
            CHECK(state.freedSize == 0);

            CHECK(state.liveSessions == 1);
            shim.fileClose(context);
            CHECK(state.liveSessions == 0);
        }

        TEST_CASE("two sessions live at once are independent and closed one at a time")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage firstImage{};
            pvdInfoImage secondImage{};
            void *first = nullptr;
            void *second = nullptr;
            openSession(shim, firstImage, first);
            openSession(shim, secondImage, second);
            REQUIRE(first != nullptr);
            REQUIRE(second != nullptr);
            CHECK(first != second);
            CHECK(state.liveSessions == 2);

            pvdInfoDecode firstDecoded{};
            pvdInfoDecode secondDecoded{};
            CHECK(shim.pageDecode(first, 0, &firstDecoded, nullptr, nullptr) == TRUE);
            CHECK(shim.pageDecode(second, 1, &secondDecoded, nullptr, nullptr) == TRUE);
            CHECK(firstDecoded.pImage != secondDecoded.pImage);
            CHECK(state.decodeCalls == 2);

            // Freeing on one context never touches the other: the second session's pixels are unknown to
            // the first and its freePage answers false (a no-op) without affecting anything.
            shim.pageFree(first, &secondDecoded);
            CHECK(state.freeCalls == 1);
            CHECK(state.freedData == reinterpret_cast<const std::byte *>(secondDecoded.pImage));
            shim.pageFree(first, &firstDecoded);
            shim.pageFree(second, &secondDecoded);
            CHECK(state.freeCalls == 3);

            shim.fileClose(first);
            CHECK(state.liveSessions == 1);
            pvdInfoPage pageInfo{};
            CHECK(shim.pageInfo(second, 1, &pageInfo) == TRUE);
            CHECK(pageInfo.lWidth == state.pageInfo.width);
            CHECK(shim.pageDecode(second, 0, &secondDecoded, nullptr, nullptr) == TRUE);
            shim.pageFree(second, &secondDecoded);

            shim.fileClose(second);
            CHECK(state.liveSessions == 0);
        }

        TEST_CASE("a decoded pitch that does not fit INT32 is refused instead of wrapping negative")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);

            state.decodedPitch = 0x7FFFFFFFU;
            pvdInfoDecode decodeInfo{};
            CHECK(shim.pageDecode(context, 0, &decodeInfo, nullptr, nullptr) == TRUE);
            CHECK(decodeInfo.lImagePitch == 0x7FFFFFFF);

            // The refused page is released by the shim itself: the host never learns its address.
            state.decodedPitch = 0x80000000U;
            pvdInfoDecode overflow{};
            CHECK(state.freeCalls == 0);
            CHECK(shim.pageDecode(context, 0, &overflow, nullptr, nullptr) == FALSE);
            CHECK(overflow.pImage == nullptr);
            CHECK(overflow.lImagePitch == 0);
            CHECK(state.freeCalls == 1);
            CHECK(state.freedData == reinterpret_cast<const std::byte *>(decodeInfo.pImage));

            shim.fileClose(context);
        }

        TEST_CASE("decoded alpha is exposed through the host's undocumented alpha flag")
        {
            test::FakeState state;
            state.decodedHasAlpha = true;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);

            pvdInfoDecode decoded{};
            REQUIRE(shim.pageDecode(context, 0, &decoded, nullptr, nullptr) == TRUE);
            CHECK(PVD_IDF_ALPHA == UINT32{2});
            CHECK(decoded.Flags == PVD_IDF_ALPHA);

            shim.pageFree(context, &decoded);
            shim.fileClose(context);
        }

        TEST_CASE("the ICC extension writer fills the observed BMP-compatible x64 layout")
        {
            constexpr std::array profile{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
            pvdInfoDecodeEx decoded{};

            detail::writeIccExtension(decoded, profile);

            CHECK(PVD_IDF_ICC_PROFILE == UINT32{4});
            CHECK(decoded.Flags == PVD_IDF_ICC_PROFILE);
            CHECK(decoded.pIccProfile == reinterpret_cast<const BYTE *>(profile.data()));
            CHECK(decoded.cbIccProfile == profile.size());
        }

        TEST_CASE("ICC extension sizes narrow exactly through the UINT32 boundary")
        {
            constexpr auto maximum = std::numeric_limits<UINT32>::max();

            REQUIRE(detail::iccExtensionSize(static_cast<std::size_t>(maximum)).has_value());
            CHECK(*detail::iccExtensionSize(static_cast<std::size_t>(maximum)) == maximum);
            if constexpr (std::numeric_limits<std::size_t>::max() > maximum) {
                CHECK_FALSE(detail::iccExtensionSize(static_cast<std::size_t>(maximum) + 1U).has_value());
            }
        }

        TEST_CASE("pageDecode writes the ICC extension only in the enabled x64 experiment build")
        {
            test::FakeState state;
            state.decodedHasAlpha = true;
            state.decodedIccProfile = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);

            const BYTE untouchedByte = 0;
            const auto *untouchedPointer = &untouchedByte;
            pvdInfoDecodeEx decoded{};
            decoded.pIccProfile = untouchedPointer;
            decoded.cbIccProfile = 0xA5A5A5A5U;
            REQUIRE(shim.pageDecode(context, 0, reinterpret_cast<pvdInfoDecode *>(&decoded), nullptr, nullptr) == TRUE);

            if (detail::iccExperimentEnabled()) {
                CHECK(decoded.Flags == (PVD_IDF_ALPHA | PVD_IDF_ICC_PROFILE));
                CHECK(decoded.pIccProfile == reinterpret_cast<const BYTE *>(state.decodedIccProfile.data()));
                CHECK(decoded.cbIccProfile == state.decodedIccProfile.size());
            } else {
                CHECK(decoded.Flags == PVD_IDF_ALPHA);
                CHECK(decoded.pIccProfile == untouchedPointer);
                CHECK(decoded.cbIccProfile == 0xA5A5A5A5U);
            }

            shim.pageFree(context, reinterpret_cast<pvdInfoDecode *>(&decoded));
            shim.fileClose(context);
        }

        TEST_CASE("an empty ICC profile never changes extension storage")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);

            const BYTE untouchedByte = 0;
            const auto *untouchedPointer = &untouchedByte;
            pvdInfoDecodeEx decoded{};
            decoded.pIccProfile = untouchedPointer;
            decoded.cbIccProfile = 0xA5A5A5A5U;
            REQUIRE(shim.pageDecode(context, 0, reinterpret_cast<pvdInfoDecode *>(&decoded), nullptr, nullptr) == TRUE);
            CHECK(decoded.Flags == 0);
            CHECK(decoded.pIccProfile == untouchedPointer);
            CHECK(decoded.cbIccProfile == 0xA5A5A5A5U);

            shim.pageFree(context, reinterpret_cast<pvdInfoDecode *>(&decoded));
            shim.fileClose(context);
        }

        TEST_CASE("fileOpen identifies memory mode and sets animated flag iff requested")
        {
            test::FakeState state;
            state.imageInfo.animated = false;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            constexpr std::array bytes{std::byte{0x0A}, std::byte{0x0B}};
            pvdInfoImage imageInfo{};
            void *context = nullptr;

            REQUIRE(shim.fileOpen("virtual.avif", 0, reinterpret_cast<const BYTE *>(bytes.data()),
                                  static_cast<UINT32>(bytes.size()), &imageInfo, &context) == TRUE);
            CHECK(state.openedFileSize == 0);
            CHECK(state.openedHead == std::vector<std::byte>{bytes.begin(), bytes.end()});
            CHECK(imageInfo.Flags == 0);
            shim.fileClose(context);
        }

        TEST_CASE("callback return value reaches the session progress object")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);
            pvdInfoDecode decodeInfo{};
            CallbackObservation callback{.result = FALSE};

            CHECK(shim.pageDecode(context, 0, &decodeInfo, observeProgress, &callback) == TRUE);
            CHECK_FALSE(state.progressResult);
            CHECK(callback.step == 4);
            CHECK(callback.steps == 8);
            shim.fileClose(context);
        }

        TEST_CASE("each expected open error maps to FALSE without a session leak")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;

            for (const auto code : test::allErrorCodes) {
                CAPTURE(core::name(code));
                state.openError = code;
                CHECK(shim.fileOpen("bad.avif", 1, nullptr, 0, &imageInfo, &context) == FALSE);
                CHECK(context == nullptr);
                CHECK(state.liveSessions == 0);
            }
        }

        TEST_CASE("each expected page error maps to FALSE")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);

            for (const auto code : test::allErrorCodes) {
                CAPTURE(core::name(code));
                state.pageInfoError = code;
                pvdInfoPage pageInfo{};
                CHECK(shim.pageInfo(context, 0, &pageInfo) == FALSE);
            }

            shim.fileClose(context);
        }

        TEST_CASE("each expected decode error maps to FALSE")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);

            for (const auto code : test::allErrorCodes) {
                CAPTURE(core::name(code));
                state.decodeError = code;
                pvdInfoDecode decodeInfo{};
                CHECK(shim.pageDecode(context, 0, &decodeInfo, nullptr, nullptr) == FALSE);
            }

            shim.fileClose(context);
        }

        TEST_CASE("null host pointers are rejected or ignored")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            pvdInfoPage pageInfo{};
            pvdInfoDecode decodeInfo{};
            BYTE decodedByte = 0;
            void *context = nullptr;

            CHECK_NOTHROW(shim.pluginInfo(nullptr));
            CHECK(shim.fileOpen(nullptr, 0, nullptr, 0, &imageInfo, &context) == FALSE);
            CHECK(shim.fileOpen("x", 0, nullptr, 1, &imageInfo, &context) == FALSE);
            CHECK(shim.fileOpen("x", -1, nullptr, 0, &imageInfo, &context) == FALSE);
            CHECK(shim.fileOpen("x", 0, nullptr, 0, nullptr, &context) == FALSE);
            CHECK(shim.fileOpen("x", 0, nullptr, 0, &imageInfo, nullptr) == FALSE);
            CHECK(shim.pageInfo(nullptr, 0, &pageInfo) == FALSE);
            CHECK(shim.pageInfo(reinterpret_cast<void *>(1), 0, nullptr) == FALSE);
            CHECK(shim.pageDecode(nullptr, 0, &decodeInfo, nullptr, nullptr) == FALSE);
            CHECK(shim.pageDecode(reinterpret_cast<void *>(1), 0, nullptr, nullptr, nullptr) == FALSE);
            CHECK_NOTHROW(shim.pageFree(nullptr, &decodeInfo));
            decodeInfo.pImage = &decodedByte;
            CHECK_NOTHROW(shim.pageFree(nullptr, &decodeInfo));
            CHECK_NOTHROW(shim.pageFree(reinterpret_cast<void *>(1), nullptr));
            CHECK_NOTHROW(shim.fileClose(nullptr));
            CHECK(state.openCalls == 0);
        }

        TEST_CASE("null decoded image is ignored by pageFree")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            openSession(shim, imageInfo, context);
            pvdInfoDecode decodeInfo{};

            shim.pageFree(context, &decodeInfo);
            CHECK(state.freeCalls == 0);
            shim.fileClose(context);
        }

        TEST_CASE("a null successful session is rejected")
        {
            test::FakeState state;
            state.returnNullSession = true;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};
            pvdInfoImage imageInfo{};
            void *context = nullptr;

            CHECK(shim.fileOpen("null.avif", 1, nullptr, 0, &imageInfo, &context) == FALSE);
            CHECK(context == nullptr);
            CHECK(state.liveSessions == 0);
        }

        TEST_CASE("exceptions from every plugin and session operation are firewalled")
        {
            test::FakeState state;
            test::FakePlugin plugin{state};
            Shim shim{plugin, kIdentity};

            // A throwing info() must leave the host with the plugin's constant identity, never with the
            // garbage it passed in: the shim fills the defaults before it asks the plugin.
            state.throwPluginInfo = true;
            pvdInfoPlugin pluginInfo{5, "garbage", "garbage", "garbage"};
            CHECK_NOTHROW(shim.pluginInfo(&pluginInfo));
            CHECK(pluginInfo.Priority == 42);
            CHECK(std::string_view{pluginInfo.pName} == "Fake");
            CHECK(std::string_view{pluginInfo.pVersion} == "9.8.7");
            CHECK(std::string_view{pluginInfo.pComments}.empty());
            state.throwPluginInfo = false;

            state.throwOpen = true;
            pvdInfoImage imageInfo{};
            void *context = nullptr;
            CHECK(shim.fileOpen("throw.avif", 1, nullptr, 0, &imageInfo, &context) == FALSE);
            CHECK(state.liveSessions == 0);
            state.throwOpen = false;

            state.throwImageInfo = true;
            CHECK(shim.fileOpen("throw.avif", 1, nullptr, 0, &imageInfo, &context) == FALSE);
            CHECK(state.liveSessions == 0);
            state.throwImageInfo = false;
            openSession(shim, imageInfo, context);

            state.throwPageInfo = true;
            pvdInfoPage pageInfo{};
            CHECK(shim.pageInfo(context, 0, &pageInfo) == FALSE);
            state.throwPageInfo = false;

            state.throwDecode = true;
            pvdInfoDecode decodeInfo{};
            CHECK(shim.pageDecode(context, 0, &decodeInfo, nullptr, nullptr) == FALSE);
            state.throwDecode = false;

            REQUIRE(shim.pageDecode(context, 0, &decodeInfo, nullptr, nullptr) == TRUE);
            state.throwFree = true;
            CHECK_NOTHROW(shim.pageFree(context, &decodeInfo));
            CHECK(state.liveSessions == 1);
            state.throwFree = false;

            shim.fileClose(context);
            CHECK(state.liveSessions == 0);
        }

    } // namespace
} // namespace pvdkit::pvd
