#include "adapters/exr/Pixels.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "adapters/exr/LuminanceChroma.hpp"
#include "core/Narrow.hpp"

namespace pvdkit::exr
{
    namespace
    {

        constexpr std::size_t kFloatsPerPixel = 4;

        /// RAII over the Core's decode pipeline: initialised on the first chunk, updated for every
        /// later one, destroyed with this object. `assign` receives every channel of the part before
        /// each run and sets (or clears) its destination; the routines are chosen once, after the
        /// first assignment, as the C++ layer does.
        class ChunkPipeline
        {
          public:
            ChunkPipeline(const ContextHandle &context, const int partIndex, const Stream &stream) noexcept
                : context_(context), partIndex_(partIndex), stream_(stream)
            {
            }

            ~ChunkPipeline()
            {
                if (initialised_) {
                    exr_decoding_destroy(context_.get(), &pipeline_);
                }
            }

            ChunkPipeline(const ChunkPipeline &) = delete;
            ChunkPipeline &operator=(const ChunkPipeline &) = delete;
            ChunkPipeline(ChunkPipeline &&) = delete;
            ChunkPipeline &operator=(ChunkPipeline &&) = delete;

            template <class Assign>
            [[nodiscard]] core::Result<void> decode(const exr_chunk_info_t &chunk, Assign &&assign)
            {
                // A failed initialize leaves the pipeline in the state exr_decoding_destroy expects
                // (it starts from the zeroed struct and frees what it allocated), so the first
                // call counts as initialised whatever it answered; a failure ends the caller's loop.
                const bool first = !initialised_;
                const auto prepared = first ? exr_decoding_initialize(context_.get(), partIndex_, &chunk, &pipeline_)
                                            : exr_decoding_update(context_.get(), partIndex_, &chunk, &pipeline_);
                initialised_ = true;
                return checkResult(prepared, stream_, core::ErrorCode::DecodeFailed)
                    .and_then([&]() -> core::Result<void> {
                        for (std::int16_t index = 0; index < pipeline_.channel_count; ++index) {
                            assign(chunk, pipeline_.channels[index]);
                        }
                        if (!first) {
                            return {};
                        }
                        return checkResult(exr_decoding_choose_default_routines(context_.get(), partIndex_, &pipeline_),
                                           stream_, core::ErrorCode::DecodeFailed);
                    })
                    .and_then([&] {
                        return checkResult(exr_decoding_run(context_.get(), partIndex_, &pipeline_), stream_,
                                           core::ErrorCode::DecodeFailed);
                    });
            }

          private:
            const ContextHandle &context_;
            int partIndex_;
            const Stream &stream_;
            // EXR_DECODE_PIPELINE_INITIALIZER is a zeroed struct with pipe_size set; exr_decoding_initialize
            // overwrites it whole, so a value-initialised struct says the same (the macro spells only
            // two of the fields and would trip the missing-initializer warning).
            exr_decode_pipeline_t pipeline_{};
            bool initialised_ = false;
        };

        /// The slot of a file channel in the interleaved RGBA scratch: R/Y/single 0, G 1, B 2, A 3.
        /// The Core refuses empty channel names (channel_list.c), so an unused slot's empty name
        /// never matches.
        std::optional<std::size_t> slotOf(const ChannelSelection &selection, const std::string_view name) noexcept
        {
            for (std::size_t slot = 0; slot < selection.colour.size(); ++slot) {
                if (selection.colour[slot] == name) {
                    return slot;
                }
            }
            if (selection.hasAlpha() && selection.alpha == name) {
                return 3;
            }
            return std::nullopt;
        }

        void clearChannel(exr_coding_channel_info_t &channel) noexcept
        {
            channel.decode_to_ptr = nullptr;
            channel.user_pixel_stride = 0;
            channel.user_line_stride = 0;
        }

        struct Cache
        {
            std::span<std::uint16_t> bgra;
            Box overlap;
            LuminanceHistogram histogram;
        };

        /// Encodes the part of one decoded chunk (float RGBA scratch, `chunkWidth` pixels per row,
        /// covering the absolute rectangle `chunk`) that lies inside the overlap. The callers only
        /// hand over chunks that touch the overlap (the scanline and tile loops walk that range;
        /// the luminance/chroma path passes the data window, which contains the overlap), so the
        /// intersection is never empty.
        void encodeChunk(const std::span<const float> scratch, const std::size_t chunkWidth, const Box &chunk,
                         const EncodeParams &params, Cache &cache)
        {
            const Box region{std::max(chunk.xMin, cache.overlap.xMin), std::max(chunk.yMin, cache.overlap.yMin),
                             std::min(chunk.xMax, cache.overlap.xMax), std::min(chunk.yMax, cache.overlap.yMax)};
            const auto columns = static_cast<std::size_t>(region.width());
            const auto overlapWidth = static_cast<std::size_t>(cache.overlap.width());
            for (std::int32_t y = region.yMin; y <= region.yMax; ++y) {
                const auto sourceOffset = (static_cast<std::size_t>(y - chunk.yMin) * chunkWidth +
                                           static_cast<std::size_t>(region.xMin - chunk.xMin)) *
                                          kFloatsPerPixel;
                const auto targetOffset = (static_cast<std::size_t>(y - cache.overlap.yMin) * overlapWidth +
                                           static_cast<std::size_t>(region.xMin - cache.overlap.xMin)) *
                                          4;
                encodePixels(scratch.subspan(sourceOffset, columns * kFloatsPerPixel), params,
                             cache.bgra.subspan(targetOffset, columns * 4), cache.histogram);
            }
        }

        /// Points every selected channel of a chunk at its slot of the float scratch and lets the
        /// Core convert to float; the other channels are skipped.
        void assignFloatSlots(const ChannelSelection &selection, std::span<float> scratch, const std::size_t chunkWidth,
                              exr_coding_channel_info_t &channel) noexcept
        {
            const auto slot = slotOf(selection, channel.channel_name);
            if (!slot) {
                clearChannel(channel);
                return;
            }
            channel.decode_to_ptr = reinterpret_cast<std::uint8_t *>(scratch.data() + *slot);
            channel.user_bytes_per_element = sizeof(float);
            channel.user_data_type = EXR_PIXEL_FLOAT;
            channel.user_pixel_stride = static_cast<std::int32_t>(kFloatsPerPixel * sizeof(float));
            channel.user_line_stride = static_cast<std::int32_t>(chunkWidth * kFloatsPerPixel * sizeof(float));
        }

        core::Result<void> readScanlines(const ContextHandle &context, const int partIndex, const Stream &stream,
                                         const PartHeader &header, const ChannelSelection &selection,
                                         const EncodeParams &params, Cache &cache)
        {
            ChunkPipeline pipeline{context, partIndex, stream};
            const auto chunkWidth = static_cast<std::size_t>(header.data.width());
            std::int32_t linesPerChunk = 0;
            auto result = checkResult(exr_get_scanlines_per_chunk(context.get(), partIndex, &linesPerChunk), stream,
                                      core::ErrorCode::DecodeFailed);
            // The Core validated the compression, whose line count is at least 1; the clamp keeps the
            // loop below finite whatever a future library answers.
            const auto step = std::max(linesPerChunk, 1);
            std::vector<float> scratch(chunkWidth * static_cast<std::size_t>(step) * kFloatsPerPixel);
            // Chunks are aligned to the data window's first row; walk those that touch the overlap.
            const auto firstChunkRow = header.data.yMin + ((cache.overlap.yMin - header.data.yMin) / step) * step;
            for (auto y = firstChunkRow; result && y <= cache.overlap.yMax; y += step) {
                exr_chunk_info_t chunk{};
                result = checkResult(exr_read_scanline_chunk_info(context.get(), partIndex, y, &chunk), stream,
                                     core::ErrorCode::DecodeFailed)
                             .and_then([&] {
                                 return pipeline.decode(
                                     chunk, [&](const exr_chunk_info_t &, exr_coding_channel_info_t &channel) {
                                         assignFloatSlots(selection, scratch, chunkWidth, channel);
                                     });
                             })
                             .transform([&] {
                                 const Box box{chunk.start_x, chunk.start_y, chunk.start_x + chunk.width - 1,
                                               chunk.start_y + chunk.height - 1};
                                 encodeChunk(scratch, chunkWidth, box, params, cache);
                             });
            }
            return result;
        }

        core::Result<void> readTiles(const ContextHandle &context, const int partIndex, const Stream &stream,
                                     const PartHeader &header, const ChannelSelection &selection,
                                     const EncodeParams &params, Cache &cache)
        {
            ChunkPipeline pipeline{context, partIndex, stream};
            const auto tileWidth = static_cast<std::int32_t>(header.tileWidth);
            const auto tileHeight = static_cast<std::int32_t>(header.tileHeight);
            std::vector<float> scratch(static_cast<std::size_t>(tileWidth) * static_cast<std::size_t>(tileHeight) *
                                       kFloatsPerPixel);
            const auto firstTileX = (cache.overlap.xMin - header.data.xMin) / tileWidth;
            const auto lastTileX = (cache.overlap.xMax - header.data.xMin) / tileWidth;
            const auto firstTileY = (cache.overlap.yMin - header.data.yMin) / tileHeight;
            const auto lastTileY = (cache.overlap.yMax - header.data.yMin) / tileHeight;
            core::Result<void> result{};
            for (auto tileY = firstTileY; result && tileY <= lastTileY; ++tileY) {
                for (auto tileX = firstTileX; result && tileX <= lastTileX; ++tileX) {
                    exr_chunk_info_t chunk{};
                    result =
                        checkResult(exr_read_tile_chunk_info(context.get(), partIndex, tileX, tileY, 0, 0, &chunk),
                                    stream, core::ErrorCode::DecodeFailed)
                            .and_then([&] {
                                return pipeline.decode(chunk, [&](const exr_chunk_info_t &info,
                                                                  exr_coding_channel_info_t &channel) {
                                    assignFloatSlots(selection, scratch, static_cast<std::size_t>(info.width), channel);
                                });
                            })
                            .transform([&] {
                                // Tile chunks report tile indices, not pixels (chunk.c).
                                const auto x0 = header.data.xMin + tileX * tileWidth;
                                const auto y0 = header.data.yMin + tileY * tileHeight;
                                const Box box{x0, y0, x0 + chunk.width - 1, y0 + chunk.height - 1};
                                encodeChunk(scratch, static_cast<std::size_t>(chunk.width), box, params, cache);
                            });
                }
            }
            return result;
        }

        /// Points Y, RY, BY and A of a scanline chunk at the half `Rgba` rows of the image: RY and BY
        /// are sampled 2x2, so they land on even rows and columns at twice the pixel and line
        /// strides. The data window starts on an even row (the Core validates the origin against
        /// the sampling, validation.c) and every multi-line scheme has an even line count, so a
        /// chunk that carries a chroma row starts on one; the single-line schemes' odd chunks
        /// carry none (the Core reports height 0) and leave the chroma channels unassigned, as
        /// the C++ layer does. Channels outside the selection are skipped.
        void assignLuminanceChroma(const ChannelSelection &selection, LuminanceChromaImage &image,
                                   const PartHeader &header, const exr_chunk_info_t &chunk,
                                   exr_coding_channel_info_t &channel) noexcept
        {
            const auto slot = slotOf(selection, channel.channel_name);
            if (!slot || channel.height == 0) {
                clearChannel(channel);
                return;
            }
            const bool chroma = *slot == 1 || *slot == 2;
            const auto sampling = chroma ? 2 : 1;
            auto row = image.row(static_cast<std::uint32_t>(chunk.start_y - header.data.yMin));
            auto &pixel = row[kChromaPad];
            std::array<Imath::half *, 4> members{&pixel.g, &pixel.r, &pixel.b, &pixel.a};
            channel.decode_to_ptr = reinterpret_cast<std::uint8_t *>(members[*slot]);
            channel.user_bytes_per_element = sizeof(Imath::half);
            channel.user_data_type = EXR_PIXEL_HALF;
            channel.user_pixel_stride = static_cast<std::int32_t>(sampling * sizeof(Imf::Rgba));
            channel.user_line_stride = static_cast<std::int32_t>(sampling * image.paddedWidth() * sizeof(Imf::Rgba));
        }

        core::Result<void> readLuminanceChroma(const ContextHandle &context, const int partIndex, const Stream &stream,
                                               const PartHeader &header, const ChannelSelection &selection,
                                               const EncodeParams &params, Cache &cache)
        {
            LuminanceChromaImage image{static_cast<std::uint32_t>(header.data.width()),
                                       static_cast<std::uint32_t>(header.data.height())};
            ChunkPipeline pipeline{context, partIndex, stream};
            std::int32_t linesPerChunk = 0;
            auto result = checkResult(exr_get_scanlines_per_chunk(context.get(), partIndex, &linesPerChunk), stream,
                                      core::ErrorCode::DecodeFailed);
            const auto step = std::max(linesPerChunk, 1);
            for (auto y = header.data.yMin; result && y <= header.data.yMax; y += step) {
                exr_chunk_info_t chunk{};
                result = checkResult(exr_read_scanline_chunk_info(context.get(), partIndex, y, &chunk), stream,
                                     core::ErrorCode::DecodeFailed)
                             .and_then([&] {
                                 return pipeline.decode(
                                     chunk, [&](const exr_chunk_info_t &info, exr_coding_channel_info_t &channel) {
                                         assignLuminanceChroma(selection, image, header, info, channel);
                                     });
                             });
            }
            return result.transform([&] {
                const auto rgba = image.reconstruct(luminanceWeights(header.chromaticities));
                encodeChunk(rgba, image.width(), header.data, params, cache);
            });
        }

    } // namespace

    core::Result<CachedPixels> readPixels(const ContextHandle &context, const int partIndex, const Stream &stream,
                                          const PartHeader &header, const ChannelSelection &selection,
                                          const Layout &layout, const EncodeParams &params)
    {
        CachedPixels cached;
        if (!layout.overlap) {
            return cached;
        }
        const auto codeCount = static_cast<std::uint64_t>(layout.overlap->width()) *
                               static_cast<std::uint64_t>(layout.overlap->height()) * 4;
        return core::narrow<std::size_t>(codeCount, "cached pixel codes").and_then([&](const std::size_t codes) {
            cached.bgra = std::make_unique_for_overwrite<std::uint16_t[]>(codes);
            cached.codes = codes;
            Cache cache{std::span{cached.bgra.get(), codes}, *layout.overlap, {}};
            const auto read = selection.kind == ChannelKind::LuminanceChroma
                                  ? readLuminanceChroma(context, partIndex, stream, header, selection, params, cache)
                              : header.tiled
                                  ? readTiles(context, partIndex, stream, header, selection, params, cache)
                                  : readScanlines(context, partIndex, stream, header, selection, params, cache);
            return read.transform([&] {
                cached.histogram = std::move(cache.histogram);
                return std::move(cached);
            });
        });
    }

} // namespace pvdkit::exr
