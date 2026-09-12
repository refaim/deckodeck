#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "core/Error.hpp"
#include "pvd/Plugin.hpp"

namespace pvdkit::pvd::test
{

    inline constexpr std::array allErrorCodes{
        core::ErrorCode::NotRecognised,    core::ErrorCode::FileOpenFailed,   core::ErrorCode::ParseFailed,
        core::ErrorCode::DecodeFailed,     core::ErrorCode::ConversionFailed, core::ErrorCode::PageOutOfRange,
        core::ErrorCode::Aborted,          core::ErrorCode::TooLarge,         core::ErrorCode::UnsupportedFeature,
        core::ErrorCode::InvalidTransform, core::ErrorCode::Internal,
    };

    struct FakeState
    {
        // Every fake registers itself here in its constructor and deregisters in its destructor, so a
        // FakeState that dies while a fake still refers to it is a test-ordering bug: the fake's
        // destructor would later write through a dangling reference. Caught here, while the counters
        // are still alive, instead of at that write.
        ~FakeState()
        {
            CHECK_MESSAGE(livePlugins == 0, "a FakePlugin outlives its FakeState");
            CHECK_MESSAGE(liveSessions == 0, "a FakeSession outlives its FakeState");
        }

        int livePlugins = 0;
        int liveSessions = 0;
        int pluginInfoCalls = 0;
        int openCalls = 0;
        int imageInfoCalls = 0;
        int pageInfoCalls = 0;
        int decodeCalls = 0;
        int freeCalls = 0;
        bool throwPluginInfo = false;
        bool throwOpen = false;
        bool throwImageInfo = false;
        bool throwPageInfo = false;
        bool throwDecode = false;
        bool throwFree = false;
        bool returnNullSession = false;
        std::optional<core::ErrorCode> openError;
        std::optional<core::ErrorCode> pageInfoError;
        std::optional<core::ErrorCode> decodeError;
        PluginInfo pluginInfo{77, "Fake AVIF", "9.8.7", "fake plugin comments"};
        ImageInfo imageInfo{2, true, "AVIF session format", "AV1 session compression", "session comments"};
        PageInfo pageInfo{320, 240, 32, 125};
        std::string openedName;
        std::uint64_t openedFileSize = 0;
        std::vector<std::byte> openedHead;
        std::uint32_t observedPage = 0;
        bool progressResult = false;
        std::uint32_t progressStep = 0;
        std::uint32_t progressSteps = 0;
        const std::byte *freedData = nullptr;
        std::size_t freedSize = 99;
        std::uint32_t decodedPitch = 8;
    };

    class FakeSession final : public IFileSession
    {
      public:
        explicit FakeSession(FakeState &state) : state_{state}, imageInfo_{state.imageInfo}
        {
            ++state_.liveSessions;
        }

        ~FakeSession() override
        {
            --state_.liveSessions;
        }

        [[nodiscard]] const ImageInfo &imageInfo() const override
        {
            ++state_.imageInfoCalls;
            if (state_.throwImageInfo) {
                throw std::runtime_error{"imageInfo failure"};
            }
            return imageInfo_;
        }

        [[nodiscard]] core::Result<PageInfo> pageInfo(const std::uint32_t page) const override
        {
            ++state_.pageInfoCalls;
            state_.observedPage = page;
            if (state_.throwPageInfo) {
                throw std::runtime_error{"pageInfo failure"};
            }
            if (state_.pageInfoError) {
                return std::unexpected{core::Error{*state_.pageInfoError, "pageInfo error"}};
            }
            return state_.pageInfo;
        }

        [[nodiscard]] core::Result<DecodedPage> decodePage(const std::uint32_t page, const Progress &progress) override
        {
            ++state_.decodeCalls;
            state_.observedPage = page;
            if (state_.throwDecode) {
                throw std::runtime_error{"decodePage failure"};
            }
            state_.progressStep = 4;
            state_.progressSteps = 8;
            state_.progressResult = progress.report(state_.progressStep, state_.progressSteps);
            if (state_.decodeError) {
                return std::unexpected{core::Error{*state_.decodeError, "decodePage error"}};
            }
            return DecodedPage{pixels_, 32, state_.decodedPitch};
        }

        bool freePage(const std::span<const std::byte> pixels) override
        {
            ++state_.freeCalls;
            if (state_.throwFree) {
                throw std::runtime_error{"freePage failure"};
            }
            state_.freedData = pixels.data();
            state_.freedSize = pixels.size();
            return pixels.data() == pixels_.data();
        }

      private:
        FakeState &state_;
        ImageInfo imageInfo_;
        std::array<std::byte, 16> pixels_{};
    };

    class FakePlugin final : public IPlugin
    {
      public:
        explicit FakePlugin(FakeState &state) : state_{state}
        {
            ++state_.livePlugins;
        }
        ~FakePlugin() override
        {
            --state_.livePlugins;
        }

        [[nodiscard]] const PluginInfo &info() const override
        {
            ++state_.pluginInfoCalls;
            if (state_.throwPluginInfo) {
                throw std::runtime_error{"plugin info failure"};
            }
            return state_.pluginInfo;
        }

        [[nodiscard]] core::Result<std::unique_ptr<IFileSession>> open(const OpenRequest &request) override
        {
            ++state_.openCalls;
            state_.openedName = request.utf8FileName;
            state_.openedFileSize = request.fileSize;
            state_.openedHead.assign(request.head.begin(), request.head.end());
            if (state_.throwOpen) {
                throw std::runtime_error{"open failure"};
            }
            if (state_.openError) {
                return std::unexpected{core::Error{*state_.openError, "open error"}};
            }
            if (state_.returnNullSession) {
                return std::unique_ptr<IFileSession>{};
            }
            std::unique_ptr<IFileSession> session = std::make_unique<FakeSession>(state_);
            return session;
        }

      private:
        FakeState &state_;
    };

} // namespace pvdkit::pvd::test
