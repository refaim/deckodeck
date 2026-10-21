#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

#include "core/IDecoder.hpp"
#include "core/Render.hpp"

struct resvg_options;
struct resvg_render_tree;

namespace pvdkit::svg
{

    struct OptionsDeleter
    {
        using pointer = resvg_options *;
        void operator()(resvg_options *options) const noexcept;
    };

    struct TreeDeleter
    {
        using pointer = resvg_render_tree *;
        void operator()(resvg_render_tree *tree) const noexcept;
    };

    using OptionsHandle = std::unique_ptr<resvg_options, OptionsDeleter>;
    using TreeHandle = std::unique_ptr<resvg_render_tree, TreeDeleter>;

    /// Owns the single font database and serialises resvg parsing. Web resources are resolved
    /// relative to the on-disk source directory when the host supplied one.
    class SvgState final
    {
      public:
        SvgState() = default;
        ~SvgState();

        SvgState(const SvgState &) = delete;
        SvgState &operator=(const SvgState &) = delete;
        SvgState(SvgState &&) = delete;
        SvgState &operator=(SvgState &&) = delete;

        [[nodiscard]] core::Result<TreeHandle> parse(std::span<const std::byte> data,
                                                     std::optional<std::wstring_view> sourcePath);

      private:
        void ensureOptions();

        mutable std::mutex mutex_;
        OptionsHandle options_;
    };

    class DecoderFactory;

    class Decoder final : public core::IDecoder
    {
      public:
        class Key
        {
            Key() = default;
            friend class DecoderFactory;
        };

        Decoder(Key, TreeHandle tree, RasterPlan plan) noexcept;

        [[nodiscard]] const core::ImageMeta &meta() const override;
        [[nodiscard]] core::Result<core::FrameTiming> frameTiming(std::uint32_t frame) const override;
        [[nodiscard]] core::Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                     std::span<std::byte> destination,
                                                     std::uint32_t pitchBytes) override;

      private:
        TreeHandle tree_;
        RasterPlan plan_;
    };

    class DecoderFactory final : public core::IDecoderFactory
    {
      public:
        explicit DecoderFactory(SvgState &state) noexcept : state_{state}
        {
        }

        DecoderFactory(const DecoderFactory &) = delete;
        DecoderFactory &operator=(const DecoderFactory &) = delete;
        DecoderFactory(DecoderFactory &&) = delete;
        DecoderFactory &operator=(DecoderFactory &&) = delete;

        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;

      private:
        SvgState &state_;
    };

} // namespace pvdkit::svg
