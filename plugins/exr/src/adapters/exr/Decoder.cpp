#include "adapters/exr/Decoder.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include <OpenEXR/openexr.h>

#include "adapters/exr/Context.hpp"
#include "adapters/exr/Pixels.hpp"
#include "core/Channels.hpp"
#include "core/Colour.hpp"
#include "core/Composite.hpp"
#include "core/Describe.hpp"
#include "core/Encode.hpp"
#include "core/Parts.hpp"
#include "core/colour/Primaries.hpp"

namespace pvdkit::exr
{
    namespace
    {

        constexpr std::array<std::byte, 4> kMagic{std::byte{0x76}, std::byte{0x2f}, std::byte{0x31}, std::byte{0x01}};
        // The format version in the low byte of the version word; 2 since OpenEXR 1.0.
        constexpr std::byte kVersion{2};
        // The magic number, the version word and the first bytes of the first attribute: no valid
        // file is shorter, and PictureView hands over at least 16 KiB (or the whole file).
        constexpr std::size_t kMinimumHead = 16;
        constexpr std::uint16_t kTransferPq = 16;

        core::Error singleFrame()
        {
            return core::Error{core::ErrorCode::PageOutOfRange, "an OpenEXR part is one still image"};
        }

        // The CICP spelling of each channel layout, indexed by ChannelKind: RGB is 4:4:4, the
        // library's luminance-chroma layout subsamples chroma 2x2, and a single channel is 4:0:0.
        constexpr std::array kChromaFormats{core::ChromaFormat::Yuv444, core::ChromaFormat::Yuv420,
                                            core::ChromaFormat::Yuv400, core::ChromaFormat::Yuv400};

        core::ChromaFormat chromaFormat(const ChannelKind kind) noexcept
        {
            return kChromaFormats[static_cast<std::size_t>(kind)];
        }

        std::string viewName(const PartHeader &header, const PartInfo &part, const ChannelSelection &selection)
        {
            if (!header.view.empty()) {
                return header.view;
            }
            if (part.multiView.empty()) {
                return {};
            }
            return selection.layer.empty() ? part.multiView.front() : selection.layer;
        }

        SourceFacts sourceFacts(const PartHeader &header, const std::vector<PartInfo> &parts, const PartChoice &choice,
                                const ColourSignal &colour)
        {
            SourceFacts facts;
            facts.selection = choice.selection;
            facts.colour = colour;
            facts.colorInteropID = header.colorInteropID;
            facts.compression = header.compression;
            facts.tiled = header.tiled;
            facts.tileWidth = header.tileWidth;
            facts.tileHeight = header.tileHeight;
            facts.levelMode = header.levelMode;
            facts.levelsX = header.levelsX;
            facts.levelsY = header.levelsY;
            facts.display = header.display;
            facts.data = header.data;
            facts.whiteLuminance = header.whiteLuminance;
            facts.partCount = parts.size();
            facts.partIndex = choice.index;
            facts.partName = header.name;
            facts.view = viewName(header, parts[choice.index], choice.selection);
            facts.stereo = !parts[choice.index].multiView.empty() ||
                           std::ranges::any_of(parts, [](const PartInfo &part) { return !part.view.empty(); });
            facts.deepSkipped = choice.deepSkipped;
            return facts;
        }

        core::ImageMeta imageMeta(const SourceFacts &facts, const Layout &layout, const float peak)
        {
            core::ImageMeta meta;
            meta.width = layout.width;
            meta.height = layout.height;
            meta.depth = facts.selection.type == PixelType::Float ? 32 : 16;
            meta.chroma = chromaFormat(facts.selection.kind);
            meta.hasAlpha = facts.selection.hasAlpha();
            meta.alphaPremultiplied = meta.hasAlpha;
            meta.cicp = core::Cicp{facts.colour.primaries, kTransferPq, 0, true};
            meta.frameCount = 1;
            meta.animated = false;
            meta.masteringPeakNits = peak;
            meta.chromaticities = facts.colour.chromaticities;
            meta.compression = std::string{compressionName(facts.compression)};
            meta.sourceDetail = describeSource(facts);
            return meta;
        }

        EncodeParams encodeParams(const PartHeader &header, const ChannelSelection &selection,
                                  const ColourSignal &colour, const PqCodeTables &tables) noexcept
        {
            return EncodeParams{nitsPerUnit(header.whiteLuminance), selection.hasAlpha(),
                                selection.kind == ChannelKind::Luminance || selection.kind == ChannelKind::Single,
                                colour.chromaticities
                                    ? core::colour::Primaries::luminanceCoefficients(*colour.chromaticities)
                                    : core::colour::Primaries::luminanceCoefficients(colour.primaries),
                                tables};
        }

    } // namespace

    core::Result<void> detail::checkDestination(const Layout &layout, const pvd::PixelFormat format,
                                                const std::size_t dstSize, const std::uint32_t pitchBytes)
    {
        if (format != pvd::PixelFormat::Bgra64) {
            return std::unexpected(core::Error{core::ErrorCode::Internal, "an EXR page is always presented as BGRA64"});
        }
        const std::uint64_t minimumPitch = static_cast<std::uint64_t>(layout.width) * 8;
        if (pitchBytes < minimumPitch) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller pitch is smaller than one pixel row"});
        }
        if (dstSize < static_cast<std::uint64_t>(pitchBytes) * layout.height) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller buffer is smaller than pitch multiplied by height"});
        }
        return {};
    }

    Decoder::Decoder(Key, core::ImageMeta meta, const Layout &layout, std::unique_ptr<std::uint16_t[]> overlap,
                     const std::size_t overlapCodes) noexcept
        : meta_(std::move(meta)), layout_(layout), overlap_(std::move(overlap)), overlapCodes_(overlapCodes)
    {
    }

    const core::ImageMeta &Decoder::meta() const
    {
        return meta_;
    }

    core::Result<core::FrameTiming> Decoder::frameTiming(const std::uint32_t frame) const
    {
        if (frame != 0) {
            return std::unexpected(singleFrame());
        }
        return core::FrameTiming{0};
    }

    core::Result<void> Decoder::decodeFrame(const std::uint32_t frame, const pvd::PixelFormat format,
                                            const std::span<std::byte> dst, const std::uint32_t pitchBytes)
    {
        if (frame != 0) {
            return std::unexpected(singleFrame());
        }
        return detail::checkDestination(layout_, format, dst.size(), pitchBytes).transform([&] {
            composite(layout_, std::span<const std::uint16_t>{overlap_.get(), overlapCodes_}, meta_.hasAlpha, dst,
                      pitchBytes);
        });
    }

    bool DecoderFactory::recognises(const std::span<const std::byte> head) const
    {
        return head.size() >= kMinimumHead && std::equal(kMagic.begin(), kMagic.end(), head.begin()) &&
               head[kMagic.size()] == kVersion;
    }

    core::Result<std::unique_ptr<core::IDecoder>> DecoderFactory::create(const std::span<const std::byte> file,
                                                                         const core::DecoderOptions &options)
    {
        if (!recognises(file)) {
            return std::unexpected(core::Error{core::ErrorCode::NotRecognised, "not an OpenEXR file"});
        }
        Stream stream{file};
        return openContext(stream, options).and_then([&](const ContextHandle &context) {
            return readParts(context, stream).and_then([&](const std::vector<PartInfo> &parts) {
                return choosePart(parts).and_then([&](const PartChoice &choice) {
                    const auto partIndex = static_cast<int>(choice.index);
                    return readHeader(context, partIndex, stream).and_then([&](PartHeader header) {
                        return layout(header.display, header.data, options).and_then([&](const Layout &layout) {
                            const auto colour = resolveColour(header.chromaticities, header.colorInteropID);
                            if (colour.unusable) {
                                // The luminance/chroma reconstruction weights come from the header's
                                // set; an unusable one is cleared so it derives Rec.709 like the rest.
                                header.chromaticities.reset();
                            }
                            const auto params = encodeParams(header, choice.selection, colour, tables_);
                            // A luminance/chroma part is reconstructed over its whole data
                            // window; the other kinds are read one chunk at a time (Windows.hpp).
                            return (choice.selection.kind == ChannelKind::LuminanceChroma
                                        ? checkDataArea(header.data, options)
                                        : core::Result<void>{})
                                .and_then([&] {
                                    return readPixels(context, partIndex, stream, header, choice.selection, layout,
                                                      params);
                                })
                                .transform([&](CachedPixels pixels) {
                                    const auto peak = peakNits(
                                        pixels.histogram, static_cast<std::uint64_t>(layout.width) * layout.height);
                                    auto meta = imageMeta(sourceFacts(header, parts, choice, colour), layout, peak);
                                    return std::unique_ptr<core::IDecoder>{std::make_unique<Decoder>(
                                        Decoder::Key{}, std::move(meta), layout, std::move(pixels.bgra), pixels.codes)};
                                });
                        });
                    });
                });
            });
        });
    }

} // namespace pvdkit::exr
