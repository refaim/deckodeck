#include "adapters/exr/Context.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include <Imath/ImathConfig.h>
#include <libdeflate.h>
#include <openjph/ojph_version.h>

namespace pvdkit::exr
{
    namespace
    {

        // The Core's stream callbacks: pread semantics over the span (a short read at the end is
        // the number of bytes available, never an error), the size query, and the error handler
        // that keeps the last message next to the bytes it concerns. They are C callbacks, so
        // raw pointers appear here and nowhere else in this file (AGENTS.md rule 3).
        std::int64_t readStream(exr_const_context_t, void *userdata, void *buffer, const std::uint64_t size,
                                const std::uint64_t offset, exr_stream_error_func_ptr_t) noexcept
        {
            const auto bytes = static_cast<const Stream *>(userdata)->bytes();
            const auto available = bytes.size() - std::min<std::uint64_t>(offset, bytes.size());
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size, available));
            if (count != 0) {
                std::memcpy(buffer, bytes.data() + offset, count);
            }
            return static_cast<std::int64_t>(count);
        }

        std::int64_t streamSize(exr_const_context_t, void *userdata) noexcept
        {
            return static_cast<std::int64_t>(static_cast<const Stream *>(userdata)->bytes().size());
        }

        // The Core reports its own size checks (data window, chunk count, tile size) through the
        // messages below; they are the plugin's limits, so they are TooLarge rather than ParseFailed.
        bool isSizeLimitMessage(const std::string_view message) noexcept
        {
            return message.find("too large") != std::string_view::npos ||
                   message.find("exceeds max") != std::string_view::npos;
        }

        int clampedLimit(const std::uint32_t value) noexcept
        {
            return static_cast<int>(std::min<std::uint32_t>(value, std::numeric_limits<int>::max()));
        }

        // PixelType is numbered like the file format's pixel types, which the Core validated.
        static_assert(static_cast<int>(PixelType::Uint) == EXR_PIXEL_UINT);
        static_assert(static_cast<int>(PixelType::Half) == EXR_PIXEL_HALF);
        static_assert(static_cast<int>(PixelType::Float) == EXR_PIXEL_FLOAT);

        PixelType pixelType(const exr_pixel_type_t type) noexcept
        {
            return static_cast<PixelType>(type);
        }

        // So are Compression and LevelMode (the Core validated both values at header time).
        static_assert(static_cast<int>(Compression::None) == EXR_COMPRESSION_NONE);
        static_assert(static_cast<int>(Compression::Dwab) == EXR_COMPRESSION_DWAB);
        static_assert(static_cast<int>(Compression::Htj2k32) == EXR_COMPRESSION_HTJ2K32);
        static_assert(static_cast<int>(LevelMode::One) == EXR_TILE_ONE_LEVEL);
        static_assert(static_cast<int>(LevelMode::Ripmap) == EXR_TILE_RIPMAP_LEVELS);

        // The Core allocates every string it parses (parse_header.c: a static buffer for a string
        // attribute, length + 1 bytes for each entry of a string vector, the part name likewise),
        // so a successful query never hands out a null pointer; the length is what the file said.
        std::string attributeString(const ContextHandle &context, const int partIndex, const std::string_view name)
        {
            std::int32_t length = 0;
            const char *text = nullptr;
            if (exr_attr_get_string(context.get(), partIndex, std::string{name}.c_str(), &length, &text) !=
                EXR_ERR_SUCCESS) {
                return {};
            }
            return std::string{text, static_cast<std::size_t>(length)};
        }

        /// The entries of a string-vector attribute; absent, or of another type, is empty.
        std::vector<std::string> attributeStrings(const ContextHandle &context, const int partIndex,
                                                  const std::string_view name)
        {
            const exr_attribute_t *attribute = nullptr;
            if (exr_get_attribute_by_name(context.get(), partIndex, std::string{name}.c_str(), &attribute) !=
                    EXR_ERR_SUCCESS ||
                attribute->type != EXR_ATTR_STRING_VECTOR) {
                return {};
            }
            const auto &vector = *attribute->stringvector;
            std::vector<std::string> strings;
            strings.reserve(static_cast<std::size_t>(std::max(vector.n_strings, 0)));
            for (std::int32_t index = 0; index < vector.n_strings; ++index) {
                const auto &entry = vector.strings[index];
                strings.emplace_back(entry.str, static_cast<std::size_t>(entry.length));
            }
            return strings;
        }

        /// One part's channel list, storage kind and view attributes.
        core::Result<PartInfo> readPart(const ContextHandle &context, const int partIndex, const Stream &stream)
        {
            exr_storage_t storage = EXR_STORAGE_SCANLINE;
            const exr_attr_chlist_t *channels = nullptr;
            return checkResult(exr_get_storage(context.get(), partIndex, &storage), stream,
                               core::ErrorCode::ParseFailed)
                .and_then([&] {
                    return checkResult(exr_get_channels(context.get(), partIndex, &channels), stream,
                                       core::ErrorCode::ParseFailed);
                })
                .transform([&] {
                    PartInfo part;
                    part.deep = storage == EXR_STORAGE_DEEP_SCANLINE || storage == EXR_STORAGE_DEEP_TILED;
                    part.view = attributeString(context, partIndex, "view");
                    part.multiView = attributeStrings(context, partIndex, "multiView");
                    for (int channel = 0; channel < channels->num_channels; ++channel) {
                        const auto &entry = channels->entries[channel];
                        part.channels.push_back(
                            ChannelInfo{std::string{entry.name.str, static_cast<std::size_t>(entry.name.length)},
                                        pixelType(entry.pixel_type), entry.x_sampling, entry.y_sampling});
                    }
                    return part;
                });
        }

        Box box(const exr_attr_box2i_t &value) noexcept
        {
            return Box{value.min.x, value.min.y, value.max.x, value.max.y};
        }

        core::Result<void> readTiling(const ContextHandle &context, const int partIndex, const Stream &stream,
                                      PartHeader &header)
        {
            std::uint32_t tileWidth = 0;
            std::uint32_t tileHeight = 0;
            exr_tile_level_mode_t levelMode = EXR_TILE_ONE_LEVEL;
            exr_tile_round_mode_t rounding = EXR_TILE_ROUND_DOWN;
            return checkResult(exr_get_tile_descriptor(context.get(), partIndex, &tileWidth, &tileHeight, &levelMode,
                                                       &rounding),
                               stream, core::ErrorCode::ParseFailed)
                .and_then([&] {
                    return checkResult(exr_get_tile_levels(context.get(), partIndex, &header.levelsX, &header.levelsY),
                                       stream, core::ErrorCode::ParseFailed);
                })
                .transform([&] {
                    header.tiled = true;
                    header.tileWidth = tileWidth;
                    header.tileHeight = tileHeight;
                    // The Core validated the mode at header time; the level modes are numbered
                    // 0, 1, 2 exactly like LevelMode.
                    header.levelMode = static_cast<LevelMode>(levelMode);
                });
        }

    } // namespace

    Stream::Stream(const std::span<const std::byte> bytes) noexcept : bytes_(bytes)
    {
    }

    std::span<const std::byte> Stream::bytes() const noexcept
    {
        return bytes_;
    }

    std::string_view Stream::message() const noexcept
    {
        return std::string_view{message_.data(), messageLength_};
    }

    void Stream::record(const std::string_view message) noexcept
    {
        const auto kept = message.substr(0, kMessageCapacity - 1);
        messageLength_ = kept.size();
        std::ranges::copy(kept, message_.begin());
        message_[messageLength_] = '\0';
    }

    void ContextFinish::operator()(exr_context_t context) const noexcept
    {
        exr_finish(&context);
    }

    core::ErrorCode errorCodeForResult(const exr_result_t result, const std::string_view message,
                                       const core::ErrorCode fallback) noexcept
    {
        if (result == EXR_ERR_FEATURE_NOT_IMPLEMENTED) {
            return core::ErrorCode::UnsupportedFeature;
        }
        if (isSizeLimitMessage(message)) {
            return core::ErrorCode::TooLarge;
        }
        return fallback;
    }

    core::Result<void> checkResult(const exr_result_t result, const Stream &stream, const core::ErrorCode fallback)
    {
        if (result == EXR_ERR_SUCCESS) {
            return {};
        }
        std::string detail{exr_get_error_code_as_string(result)};
        if (!stream.message().empty()) {
            detail += ": ";
            detail += stream.message();
        }
        return std::unexpected(core::Error{errorCodeForResult(result, stream.message(), fallback), std::move(detail)});
    }

    core::Result<ContextHandle> openContext(Stream &stream, const core::DecoderOptions &options)
    {
        exr_context_initializer_t initializer = EXR_DEFAULT_CONTEXT_INITIALIZER;
        initializer.error_handler_fn = &detail::recordError;
        initializer.user_data = &stream;
        initializer.read_fn = &readStream;
        initializer.size_fn = &streamSize;
        initializer.max_image_width = clampedLimit(options.maxDimension);
        initializer.max_image_height = clampedLimit(options.maxDimension);
        initializer.max_tile_width = kMaxTileSide;
        initializer.max_tile_height = kMaxTileSide;
        exr_context_t context = nullptr;
        const auto result = exr_start_read(&context, "memory", &initializer);
        ContextHandle handle{context};
        return checkResult(result, stream, core::ErrorCode::ParseFailed).transform([&] { return std::move(handle); });
    }

    core::Result<std::vector<PartInfo>> readParts(const ContextHandle &context, const Stream &stream)
    {
        int count = 0;
        return checkResult(exr_get_count(context.get(), &count), stream, core::ErrorCode::ParseFailed).and_then([&] {
            return detail::readParts(context, count, stream);
        });
    }

    core::Result<std::vector<PartInfo>> detail::readParts(const ContextHandle &context, const int count,
                                                          const Stream &stream)
    {
        std::vector<PartInfo> parts;
        for (int partIndex = 0; partIndex < count; ++partIndex) {
            auto part = readPart(context, partIndex, stream);
            if (!part) {
                return std::unexpected(std::move(part).error());
            }
            parts.push_back(std::move(*part));
        }
        return parts;
    }

    void detail::recordError(exr_const_context_t context, exr_result_t, const char *message) noexcept
    {
        if (context == nullptr || message == nullptr) {
            return;
        }
        // Every context the plugin opens carries its Stream (openContext), and the Core sets the
        // user data before it can report anything on the context (internal_structs.c); the query
        // only fails for a null context, refused above.
        void *userdata = nullptr;
        static_cast<void>(exr_get_user_data(context, &userdata));
        static_cast<Stream *>(userdata)->record(std::string_view{message});
    }

    core::Result<PartHeader> readHeader(const ContextHandle &context, const int partIndex, const Stream &stream)
    {
        PartHeader header;
        exr_attr_box2i_t display{};
        exr_attr_box2i_t data{};
        exr_compression_t compression = EXR_COMPRESSION_NONE;
        exr_storage_t storage = EXR_STORAGE_SCANLINE;
        return checkResult(exr_get_display_window(context.get(), partIndex, &display), stream,
                           core::ErrorCode::ParseFailed)
            .and_then([&] {
                return checkResult(exr_get_data_window(context.get(), partIndex, &data), stream,
                                   core::ErrorCode::ParseFailed);
            })
            .and_then([&] {
                return checkResult(exr_get_lineorder(context.get(), partIndex, &header.lineOrder), stream,
                                   core::ErrorCode::ParseFailed);
            })
            .and_then([&] {
                return checkResult(exr_get_compression(context.get(), partIndex, &compression), stream,
                                   core::ErrorCode::ParseFailed);
            })
            .and_then([&] {
                return checkResult(exr_get_storage(context.get(), partIndex, &storage), stream,
                                   core::ErrorCode::ParseFailed);
            })
            .and_then([&]() -> core::Result<void> {
                if (storage != EXR_STORAGE_TILED) {
                    return {};
                }
                return readTiling(context, partIndex, stream, header);
            })
            .transform([&] {
                header.display = box(display);
                header.data = box(data);
                // The compression enumerators are the file format's numbering, shared with Compression.
                header.compression = static_cast<Compression>(compression);
                exr_attr_chromaticities_t chromaticities{};
                if (exr_attr_get_chromaticities(context.get(), partIndex, "chromaticities", &chromaticities) ==
                    EXR_ERR_SUCCESS) {
                    header.chromaticities =
                        core::colour::Primaries::Chromaticities{{chromaticities.red_x, chromaticities.red_y},
                                                                {chromaticities.green_x, chromaticities.green_y},
                                                                {chromaticities.blue_x, chromaticities.blue_y},
                                                                {chromaticities.white_x, chromaticities.white_y}};
                }
                float whiteLuminance = 0.0F;
                if (exr_attr_get_float(context.get(), partIndex, "whiteLuminance", &whiteLuminance) ==
                    EXR_ERR_SUCCESS) {
                    header.whiteLuminance = whiteLuminance;
                }
                header.colorInteropID = attributeString(context, partIndex, "colorInteropID");
                header.view = attributeString(context, partIndex, "view");
                const char *name = nullptr;
                if (exr_get_name(context.get(), partIndex, &name) == EXR_ERR_SUCCESS) {
                    header.name = name;
                }
                return header;
            });
    }

    std::string libraryVersions()
    {
        int major = 0;
        int minor = 0;
        int patch = 0;
        const char *extra = nullptr;
        exr_get_library_version(&major, &minor, &patch, &extra);
        return "OpenEXR " + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch) +
               ", Imath " + std::string{IMATH_VERSION_STRING} + ", libdeflate " +
               std::string{LIBDEFLATE_VERSION_STRING} + ", OpenJPH " + std::to_string(OPENJPH_VERSION_MAJOR) + "." +
               std::to_string(OPENJPH_VERSION_MINOR) + "." + std::to_string(OPENJPH_VERSION_PATCH);
    }

} // namespace pvdkit::exr
