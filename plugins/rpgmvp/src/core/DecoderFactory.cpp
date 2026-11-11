#include "core/DecoderFactory.hpp"

namespace pvdkit::rpgmvp
{

    CombinedDecoderFactory::CombinedDecoderFactory(core::IDecoderFactory &png, core::IDecoderFactory &webp) noexcept
        : png_(png), webp_(webp)
    {
    }

    bool CombinedDecoderFactory::recognises(const std::span<const std::byte> head) const
    {
        return png_.recognises(head) || webp_.recognises(head);
    }

    core::Result<std::unique_ptr<core::IDecoder>> CombinedDecoderFactory::create(const std::span<const std::byte> file,
                                                                                 const core::DecoderOptions &options)
    {
        if (png_.recognises(file)) {
            return png_.create(file, options);
        }
        return webp_.create(file, options);
    }

} // namespace pvdkit::rpgmvp
