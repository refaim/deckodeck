#pragma once

#include "core/IDecoder.hpp"

namespace pvdkit::rpgmvp
{

    class CombinedDecoderFactory final : public core::IDecoderFactory
    {
      public:
        CombinedDecoderFactory(core::IDecoderFactory &png, core::IDecoderFactory &webp) noexcept;
        CombinedDecoderFactory(const CombinedDecoderFactory &) = delete;
        CombinedDecoderFactory &operator=(const CombinedDecoderFactory &) = delete;
        CombinedDecoderFactory(CombinedDecoderFactory &&) = delete;
        CombinedDecoderFactory &operator=(CombinedDecoderFactory &&) = delete;

        [[nodiscard]] bool recognises(std::span<const std::byte> head) const override;
        [[nodiscard]] core::Result<std::unique_ptr<core::IDecoder>> create(
            std::span<const std::byte> file, const core::DecoderOptions &options) override;

      private:
        core::IDecoderFactory &png_;
        core::IDecoderFactory &webp_;
    };

} // namespace pvdkit::rpgmvp
