#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <OpenEXR/openexr.h>

#include "core/Channels.hpp"
#include "core/Describe.hpp"
#include "core/Error.hpp"
#include "core/IDecoder.hpp"
#include "core/Parts.hpp"
#include "core/Windows.hpp"
#include "core/colour/Primaries.hpp"

// The OpenEXRCore boundary: a read context over the file bytes (the Core's stream callbacks
// serve the span; no file name reaches the library), the header facts of every part, and the
// mapping of the Core's result codes. Only OpenEXRCore's C API is used here: the C++ layer
// (Imf::InputFile and friends) reports errors by throwing, which no adapter may catch
// (AGENTS.md rule 5), and its thread pool is a function-local static the plugin must not link
// (rule 13); the Core reports by return code and owns no process-wide state a context needs.
// The context travels through our functions as the owning handle (`const ContextHandle &`) and
// becomes a raw pointer only on the line that calls the Core (rule 3); the three C callbacks
// registered with the Core keep its signatures, they are the boundary itself.
namespace pvdkit::exr
{

    /// The bytes a context reads and the last message the Core's error handler delivered for it.
    /// The message lives in a fixed buffer: the handler is a C callback, noexcept, and may not
    /// allocate; the Core's messages are one line (its own stack buffer is 256 bytes), longer ones
    /// are cut at the capacity.
    class Stream
    {
      public:
        static constexpr std::size_t kMessageCapacity = 512;

        explicit Stream(std::span<const std::byte> bytes) noexcept;

        [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
        /// The last recorded message; empty before the first.
        [[nodiscard]] std::string_view message() const noexcept;
        /// Keeps up to kMessageCapacity - 1 bytes of `message`.
        void record(std::string_view message) noexcept;

      private:
        std::span<const std::byte> bytes_;
        std::array<char, kMessageCapacity> message_{};
        std::size_t messageLength_ = 0;
    };

    struct ContextFinish
    {
        void operator()(exr_context_t context) const noexcept;
    };

    using ContextHandle = std::unique_ptr<std::remove_pointer_t<exr_context_t>, ContextFinish>;

    /// Tile chunks decode into a scratch of tile width x height x 16 bytes; the Core refuses tiles
    /// beyond this side length at header time (per context, not the process-wide default).
    inline constexpr int kMaxTileSide = 4'096;

    /// The header facts of the part being displayed.
    struct PartHeader
    {
        Box display;
        Box data;
        exr_lineorder_t lineOrder = EXR_LINEORDER_INCREASING_Y;
        Compression compression = Compression::None;
        bool tiled = false;
        std::uint32_t tileWidth = 0;
        std::uint32_t tileHeight = 0;
        LevelMode levelMode = LevelMode::One;
        std::int32_t levelsX = 1;
        std::int32_t levelsY = 1;
        std::optional<core::colour::Primaries::Chromaticities> chromaticities;
        std::optional<float> whiteLuminance;
        std::string colorInteropID;
        std::string name;
        std::string view;
    };

    /// Maps a Core result to the plugin's error category: `EXR_ERR_FEATURE_NOT_IMPLEMENTED` is
    /// `UnsupportedFeature`, a header rejection whose message names a size limit is `TooLarge`,
    /// anything else - `EXR_ERR_OUT_OF_MEMORY` included - is `fallback`. The Core answers
    /// OUT_OF_MEMORY for hostile data too (a corrupt deflate stream that inflates past the chunk
    /// size is one, compression.c), so it is an expected failure of the file, never an exception.
    [[nodiscard]] core::ErrorCode errorCodeForResult(exr_result_t result, std::string_view message,
                                                     core::ErrorCode fallback) noexcept;
    /// Success, or the mapped error carrying the Core's message.
    [[nodiscard]] core::Result<void> checkResult(exr_result_t result, const Stream &stream, core::ErrorCode fallback);

    /// Opens a read context over `stream.bytes()` (which must outlive the context) with the
    /// per-context size limits from `options` and the error handler that records on `stream`.
    [[nodiscard]] core::Result<ContextHandle> openContext(Stream &stream, const core::DecoderOptions &options);

    /// The channel lists, storage kinds and view attributes of every part, for `choosePart`.
    [[nodiscard]] core::Result<std::vector<PartInfo>> readParts(const ContextHandle &context, const Stream &stream);

    /// The remaining header facts of one part.
    [[nodiscard]] core::Result<PartHeader> readHeader(const ContextHandle &context, int partIndex,
                                                      const Stream &stream);

    /// The run-time versions of the linked libraries, as the plugin comments report them.
    [[nodiscard]] std::string libraryVersions();

    // Internal helpers exposed for unit tests; not part of the adapter contract.
    namespace detail
    {

        /// The Core's error handler: keeps `message` on the `Stream` the context was opened with.
        /// The Core calls it with a null context for allocation failures and bad arguments
        /// (context.c, internal_structs.c); that and a null message leave the stream as it is.
        void recordError(exr_const_context_t context, exr_result_t result, const char *message) noexcept;

        /// The first `count` parts; `readParts` passes the Core's own count, so an index beyond it
        /// is only reachable from here (the Core answers ARGUMENT_OUT_OF_RANGE).
        [[nodiscard]] core::Result<std::vector<PartInfo>> readParts(const ContextHandle &context, int count,
                                                                    const Stream &stream);

    } // namespace detail

} // namespace pvdkit::exr
