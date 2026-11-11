#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

#include <doctest/doctest.h>

#include "core/DecoderFactory.hpp"

namespace pvdkit::rpgmvp
{
    namespace
    {

        class RoutedDecoder final : public core::IDecoder
        {
          public:
            explicit RoutedDecoder(const std::uint32_t width)
            {
                meta_.width = width;
            }

            [[nodiscard]] const core::ImageMeta &meta() const override
            {
                return meta_;
            }

            [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t) const override
            {
                return core::FrameTiming{0};
            }

            [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t, pvd::PixelFormat, std::span<std::byte>,
                                                         std::uint32_t) override
            {
                return {};
            }

          private:
            core::ImageMeta meta_;
        };

        class RoutedFactory final : public core::IDecoderFactory
        {
          public:
            bool recognised = false;
            std::uint32_t width = 0;
            mutable unsigned recognitionCalls = 0;
            unsigned creationCalls = 0;
            mutable std::span<const std::byte> observedHead;
            std::span<const std::byte> observedFile;
            core::DecoderOptions observedOptions;
            std::optional<core::Error> error;

            [[nodiscard]] bool recognises(const std::span<const std::byte> head) const override
            {
                ++recognitionCalls;
                observedHead = head;
                return recognised;
            }

            [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
                const std::span<const std::byte> file, const core::DecoderOptions &options) override
            {
                ++creationCalls;
                observedFile = file;
                observedOptions = options;
                if (error) {
                    return std::unexpected(*error);
                }
                return std::make_unique<RoutedDecoder>(width);
            }
        };

        TEST_CASE("combined RPGMVP recognition accepts PNG or WebP and preserves PNG priority")
        {
            constexpr std::array head{std::byte{0x52}, std::byte{0x50}};
            RoutedFactory png;
            RoutedFactory webp;
            const CombinedDecoderFactory factory{png, webp};
            static_assert(!std::is_copy_constructible_v<CombinedDecoderFactory>);
            static_assert(!std::is_copy_assignable_v<CombinedDecoderFactory>);
            static_assert(!std::is_move_constructible_v<CombinedDecoderFactory>);
            static_assert(!std::is_move_assignable_v<CombinedDecoderFactory>);

            CHECK_FALSE(factory.recognises(head));
            CHECK(png.recognitionCalls == 1);
            CHECK(webp.recognitionCalls == 1);
            CHECK(png.observedHead.data() == head.data());
            CHECK(png.observedHead.size() == head.size());
            CHECK(webp.observedHead.data() == head.data());
            CHECK(webp.observedHead.size() == head.size());

            webp.recognised = true;
            CHECK(factory.recognises(head));
            CHECK(png.recognitionCalls == 2);
            CHECK(webp.recognitionCalls == 2);

            png.recognised = true;
            CHECK(factory.recognises(head));
            CHECK(png.recognitionCalls == 3);
            CHECK(webp.recognitionCalls == 2);
        }

        TEST_CASE("combined RPGMVP creation forwards the original input and options to the selected decoder")
        {
            constexpr std::array file{std::byte{0x52}, std::byte{0x50}, std::byte{0x47}};
            constexpr core::DecoderOptions options{3, true, 12'345, 432, true, true, L"C:\\input.png_"};
            RoutedFactory png;
            RoutedFactory webp;
            png.width = 11;
            webp.width = 22;
            CombinedDecoderFactory factory{png, webp};

            auto fromWebp = factory.create(file, options);
            REQUIRE(fromWebp.has_value());
            CHECK((*fromWebp)->meta().width == 22);
            CHECK(png.creationCalls == 0);
            CHECK(webp.creationCalls == 1);
            CHECK(webp.observedFile.data() == file.data());
            CHECK(webp.observedFile.size() == file.size());
            CHECK(webp.observedOptions.maxThreads == 3);
            CHECK(webp.observedOptions.strict);
            CHECK(webp.observedOptions.maxPixels == 12'345);
            CHECK(webp.observedOptions.maxDimension == 432);
            CHECK(webp.observedOptions.deepOutput);
            CHECK(webp.observedOptions.nativeRgbOutput);
            CHECK(webp.observedOptions.sourcePath == options.sourcePath);

            png.recognised = true;
            auto fromPng = factory.create(file, options);
            REQUIRE(fromPng.has_value());
            CHECK((*fromPng)->meta().width == 11);
            CHECK(png.creationCalls == 1);
            CHECK(webp.creationCalls == 1);
            CHECK(png.observedFile.data() == file.data());
            CHECK(png.observedFile.size() == file.size());
            CHECK(png.observedOptions.maxThreads == options.maxThreads);
            CHECK(png.observedOptions.sourcePath == options.sourcePath);
        }

        TEST_CASE("combined RPGMVP creation retains the selected decoder error")
        {
            RoutedFactory png;
            RoutedFactory webp;
            CombinedDecoderFactory factory{png, webp};
            const core::DecoderOptions options;

            webp.error = core::Error{core::ErrorCode::NotRecognised, "not an encrypted WebP"};
            auto unknown = factory.create({}, options);
            REQUIRE_FALSE(unknown.has_value());
            CHECK(unknown.error().code == core::ErrorCode::NotRecognised);
            CHECK(unknown.error().detail == "not an encrypted WebP");

            png.recognised = true;
            png.error = core::Error{core::ErrorCode::ParseFailed, "broken PNG"};
            auto broken = factory.create({}, options);
            REQUIRE_FALSE(broken.has_value());
            CHECK(broken.error().code == core::ErrorCode::ParseFailed);
            CHECK(broken.error().detail == "broken PNG");
            CHECK(webp.creationCalls == 1);
        }

    } // namespace
} // namespace pvdkit::rpgmvp
