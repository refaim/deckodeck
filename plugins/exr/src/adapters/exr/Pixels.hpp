#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <OpenEXR/openexr.h>

#include "adapters/exr/Context.hpp"
#include "core/Channels.hpp"
#include "core/Encode.hpp"
#include "core/Error.hpp"
#include "core/Windows.hpp"

namespace pvdkit::exr
{

    /// The picture as the decoder keeps it for the session: the overlap of the data and display
    /// windows as BGRA64 PQ codes (`layout.overlap` width x height x 4, row-major; empty without an
    /// overlap) plus the luminance histogram the tone-mapping peak is read from.
    struct CachedPixels
    {
        std::unique_ptr<std::uint16_t[]> bgra;
        std::size_t codes = 0;
        LuminanceHistogram histogram;
    };

    /// Reads the selected channels of one part through the Core's chunk pipeline - scanline chunks
    /// or level-0 tiles, converted to float by the Core, one chunk of scratch at a time - and
    /// encodes them (plugins/exr/DESIGN.md, "Channels", "Windows"). Luminance-chroma parts are read
    /// whole as half and reconstructed with the library's RgbaYca filters first, weighted by
    /// `header.chromaticities` (the caller has validated the set or cleared it: Decoder.cpp).
    [[nodiscard]] core::Result<CachedPixels> readPixels(const ContextHandle &context, int partIndex,
                                                        const Stream &stream, const PartHeader &header,
                                                        const ChannelSelection &selection, const Layout &layout,
                                                        const EncodeParams &params);

} // namespace pvdkit::exr
