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
            facts.view = header.view;
            facts.stereo = std::ranges::any_of(
                parts, [](const PartInfo &part) { return !part.multiView.empty() || !part.view.empty(); });
            facts.deepSkipped = choice.deepSkipped;
            return facts;
        }

        core::ImageMeta imageMeta(const SourceFacts &facts, const Layout &layout, const std::optional<float> peak)
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
                                  const ColourSignal &colour, const PqCodeTables &tables, const bool native) noexcept
        {
            return EncodeParams{nitsPerUnit(header.whiteLuminance),
                                selection.hasAlpha(),
                                selection.kind == ChannelKind::Luminance || selection.kind == ChannelKind::Single,
                                colour.chromaticities
                                    ? core::colour::Primaries::luminanceCoefficients(*colour.chromaticities)
                                    : core::colour::Primaries::luminanceCoefficients(colour.primaries),
                                tables,
                                native ? (selection.hasAlpha() ? pvd::PixelFormat::Rgba64 : pvd::PixelFormat::Rgb48)
                                       : pvd::PixelFormat::Bgra64};
        }

        core::Result<Page> readPage(const ContextHandle &context, const Stream &stream,
                                    const std::vector<PartInfo> &parts, PageChoice choice, const std::uint32_t count,
                                    const core::DecoderOptions &options, const PqCodeTables &tables)
        {
            return readHeader(context, static_cast<int>(choice.part.index), stream).and_then([&](PartHeader header) {
                return layout(header.display, header.data, options).and_then([&](const Layout &shape) {
                    const auto colour = resolveColour(header.chromaticities, header.colorInteropID);
                    auto facts = sourceFacts(header, parts, choice.part, colour);
                    facts.pageCount = count;
                    facts.view = choice.view;
                    auto meta = imageMeta(facts, shape, std::nullopt);
                    meta.frameCount = count;
                    if (colour.unusable) {
                        header.chromaticities.reset();
                    } else if (!header.chromaticities) {
                        header.chromaticities = interopChromaticities(header.colorInteropID);
                    }
                    return (choice.part.selection.kind == ChannelKind::LuminanceChroma
                                ? checkDataArea(header.data, options)
                                : core::Result<void>{})
                        .transform([&] {
                            const auto params =
                                encodeParams(header, choice.part.selection, colour, tables, options.nativeRgbOutput);
                            return Page{std::move(choice), std::move(header), colour, shape,
                                        std::move(meta),   params.format};
                        });
                });
            });
        }
    } // namespace

    core::Result<void> detail::checkDestination(const Layout &layout, const pvd::PixelFormat format,
                                                const std::size_t dstSize, const std::uint32_t pitchBytes,
                                                const pvd::PixelFormat selectedFormat)
    {
        if (format != selectedFormat) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller format differs from the cached EXR layout"});
        }
        const std::uint64_t minimumPitch =
            static_cast<std::uint64_t>(layout.width) * pvd::pixelLayout(selectedFormat).bytesPerPixel;
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

    Decoder::Decoder(Key, std::vector<Page> pages, const std::span<const std::byte> file,
                     const core::DecoderOptions &options, const PqCodeTables &tables, CachedPixels pixels) noexcept
        : pages_(std::move(pages)), file_(file), options_(options), tables_(tables), overlap_(std::move(pixels.pixels)),
          overlapCodes_(pixels.codes), decodedColour_{meta().cicp, meta().masteringPeakNits, meta().chromaticities}
    {
    }

    const core::ImageMeta &Decoder::meta() const
    {
        return pages_.front().meta;
    }

    core::Result<core::PageMeta> Decoder::pageMeta(const std::uint32_t page) const
    {
        if (page >= pages_.size()) {
            return std::unexpected(singleFrame());
        }
        return core::PageMeta{pages_[page].meta, true};
    }

    core::ColourEncoding Decoder::decodedColour() const
    {
        return decodedColour_;
    }

    core::Result<core::FrameTiming> Decoder::frameTiming(const std::uint32_t frame) const
    {
        if (frame >= pages_.size()) {
            return std::unexpected(singleFrame());
        }
        return core::FrameTiming{0};
    }

    core::Result<void> Decoder::loadPage(const std::uint32_t page)
    {
        if (current_ == page) {
            return {};
        }
        // Evict before allocating the next cache; a failed lazy read can be retried.
        overlap_.reset();
        overlapCodes_ = 0;
        current_.reset();
        auto &selected = pages_[page];
        Stream stream{file_};
        return openContext(stream, options_)
            .and_then([&](const ContextHandle &context) {
                const auto params = encodeParams(selected.header, selected.choice.part.selection, selected.colour,
                                                 tables_, options_.nativeRgbOutput);
                return readPixels(context, static_cast<int>(selected.choice.part.index), stream, selected.header,
                                  selected.choice.part.selection, selected.layout, params);
            })
            .transform([&](CachedPixels pixels) {
                selected.meta.masteringPeakNits = peakNits(
                    pixels.histogram, static_cast<std::uint64_t>(selected.layout.width) * selected.layout.height);
                overlap_ = std::move(pixels.pixels);
                overlapCodes_ = pixels.codes;
                current_ = page;
            });
    }

    core::Result<void> Decoder::decodeFrame(const std::uint32_t frame, const pvd::PixelFormat format,
                                            const std::span<std::byte> dst, const std::uint32_t pitchBytes)
    {
        if (frame >= pages_.size()) {
            return std::unexpected(singleFrame());
        }
        const auto &page = pages_[frame];
        return detail::checkDestination(page.layout, format, dst.size(), pitchBytes, page.format)
            .and_then([&] { return loadPage(frame); })
            .transform([&] {
                composite(page.layout, std::span<const std::uint16_t>{overlap_.get(), overlapCodes_},
                          page.meta.hasAlpha, dst, pitchBytes, page.format);
                decodedColour_ = {page.meta.cicp, page.meta.masteringPeakNits, page.meta.chromaticities};
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
                std::vector<std::vector<ChannelGroup>> groups;
                groups.reserve(parts.size());
                for (const auto &part : parts) {
                    groups.push_back(channelGroups(part));
                }
                return pageChoices(parts, groups)
                    .and_then([&](std::vector<PageChoice> choices) -> core::Result<std::unique_ptr<core::IDecoder>> {
                        std::vector<Page> pages;
                        for (auto &choice : choices) {
                            auto page = readPage(context, stream, parts, std::move(choice),
                                                 static_cast<std::uint32_t>(choices.size()), options, tables_);
                            if (!page) {
                                return std::unexpected(std::move(page).error());
                            }
                            pages.push_back(std::move(*page));
                        }
                        auto &first = pages.front();
                        const auto params = encodeParams(first.header, first.choice.part.selection, first.colour,
                                                         tables_, options.nativeRgbOutput);
                        return readPixels(context, static_cast<int>(first.choice.part.index), stream, first.header,
                                          first.choice.part.selection, first.layout, params)
                            .transform([&](CachedPixels pixels) {
                                first.meta.masteringPeakNits =
                                    peakNits(pixels.histogram,
                                             static_cast<std::uint64_t>(first.layout.width) * first.layout.height);
                                return std::unique_ptr<core::IDecoder>{std::make_unique<Decoder>(
                                    Decoder::Key{}, std::move(pages), file, options, tables_, std::move(pixels))};
                            });
                    });
            });
        });
    }
} // namespace pvdkit::exr
