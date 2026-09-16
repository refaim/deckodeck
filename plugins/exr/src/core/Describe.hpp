#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/Channels.hpp"
#include "core/Colour.hpp"
#include "core/IDecoder.hpp"
#include "core/IImageDescriber.hpp"
#include "core/Windows.hpp"

namespace pvdkit::exr
{

    /// The OpenEXR compression schemes, in the order of the file format's `compression` attribute.
    enum class Compression : std::uint8_t
    {
        None,
        Rle,
        Zips,
        Zip,
        Piz,
        Pxr24,
        B44,
        B44a,
        Dwaa,
        Dwab,
        Htj2k256,
        Htj2k32
    };

    /// The tile level layout of a tiled part.
    enum class LevelMode : std::uint8_t
    {
        One,
        Mipmap,
        Ripmap
    };

    /// Everything the info line says about the file, as the adapter read it from the header.
    struct SourceFacts
    {
        ChannelSelection selection;
        ColourSignal colour;
        std::string colorInteropID;
        Compression compression = Compression::None;
        bool tiled = false;
        std::uint32_t tileWidth = 0;
        std::uint32_t tileHeight = 0;
        LevelMode levelMode = LevelMode::One;
        std::int32_t levelsX = 1;
        std::int32_t levelsY = 1;
        Box display;
        Box data;
        std::optional<float> whiteLuminance;
        std::size_t partCount = 1;
        std::size_t partIndex = 0;
        std::string partName;
        std::string view;    ///< The chosen part's `view` attribute, or the view its channels belong to.
        bool stereo = false; ///< The file carries several views (`multiView`, or parts with `view`).
        bool deepSkipped = false;
    };

    [[nodiscard]] std::string_view compressionName(Compression compression) noexcept;
    /// The source half of the comments line, e.g.
    /// `half RGBA, linear Rec.709, ZIP, scanline, display 1920x1080, data 1920x1080 at 0,0, 100 nit white`.
    [[nodiscard]] std::string describeSource(const SourceFacts &facts);
    /// The presentation half, from what the decoder handed the shared core:
    /// `→ sRGB (BT.2390 tone map from 1250 nit)` or `→ sRGB (SDR range, no tone map)`.
    [[nodiscard]] std::string presentationNote(const core::ImageMeta &meta);
    /// `meta.sourceDetail` followed by the presentation note.
    [[nodiscard]] std::string describe(const core::ImageMeta &meta);

    /// The OpenEXR `IImageDescriber`: format "OpenEXR", compression from the meta, comments from `describe`.
    class Describer final : public core::IImageDescriber
    {
      public:
        [[nodiscard]] core::ImageDescription describe(const core::ImageMeta &meta) const override;
    };

} // namespace pvdkit::exr
