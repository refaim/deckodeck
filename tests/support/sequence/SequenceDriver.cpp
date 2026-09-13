#include "sequence/SequenceDriver.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace pvdkit::sequence
{
    namespace
    {

        constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        std::optional<std::string> checkHostString(const char *value, const std::string_view field)
        {
            if (value == nullptr) {
                return std::string{field};
            }
            for (std::size_t index = 0; index < kMaxStringBytes; ++index) {
                if (value[index] == '\0') {
                    return std::nullopt;
                }
            }
            return std::string{field};
        }

        std::uint64_t fnv1a(const std::span<const std::byte> input) noexcept
        {
            std::uint64_t hash = kFnvOffset;
            for (const std::byte value : input) {
                hash ^= std::to_integer<std::uint8_t>(value);
                hash *= kFnvPrime;
            }
            return hash;
        }

        struct CallbackState
        {
            bool abortEnabled;
            std::uint32_t abortAtCall;
            std::uint32_t calls = 0;
            bool aborted = false;
        };

        BOOL __stdcall progressCallback(void *context, const UINT32 step, const UINT32 steps) noexcept
        {
            auto &state = *static_cast<CallbackState *>(context);
            enforceInvariant(checkProgress(step, steps));
            ++state.calls;
            if (state.abortEnabled && state.calls == state.abortAtCall) {
                state.aborted = true;
                return FALSE;
            }
            return TRUE;
        }

        void touchImage(const pvdInfoPage &page, const pvdInfoDecode &decoded)
        {
            const auto rowBytes =
                static_cast<std::size_t>(static_cast<std::uint64_t>(page.lWidth) * (decoded.nBPP / 8U));
            std::uint64_t digest = kFnvOffset;
            for (std::uint32_t rowIndex = 0; rowIndex < page.lHeight; ++rowIndex) {
                const auto row = rowSpan(decoded, page.lHeight, rowIndex).value().first(rowBytes);
                for (const std::byte value : row) {
                    digest ^= std::to_integer<std::uint8_t>(value);
                    digest *= kFnvPrime;
                }
            }
            // Keep the reads observable so optimized tests really touch every pixel byte, including
            // the first and last logical rows, before the host returns the page.
            static std::atomic<std::uint64_t> touchedImageDigest{};
            touchedImageDigest.fetch_xor(digest, std::memory_order_relaxed);
        }

    } // namespace

    std::optional<RowSpan> rowSpan(const pvdInfoDecode &decoded, const std::uint32_t height,
                                   const std::uint32_t row) noexcept
    {
        return rowSpan(decoded, height, row, std::numeric_limits<std::size_t>::max());
    }

    std::optional<RowSpan> rowSpan(const pvdInfoDecode &decoded, const std::uint32_t height, const std::uint32_t row,
                                   const std::size_t maximumBufferBytes) noexcept
    {
        if (decoded.pImage == nullptr || height == 0 || row >= height) {
            return std::nullopt;
        }

        const auto signedPitch = static_cast<std::int64_t>(decoded.lImagePitch);
        const auto pitchMagnitude = static_cast<std::uint64_t>(signedPitch < 0 ? -signedPitch : signedPitch);
        const auto bufferBytes64 = pitchMagnitude * static_cast<std::uint64_t>(height);
        if (bufferBytes64 > maximumBufferBytes) {
            return std::nullopt;
        }
        const auto bufferBytes = static_cast<std::size_t>(bufferBytes64);
        const auto origin = reinterpret_cast<std::uintptr_t>(decoded.pImage);
        if (bufferBytes > std::numeric_limits<std::uintptr_t>::max() - origin) {
            return std::nullopt;
        }

        const auto physicalRow =
            signedPitch < 0 ? static_cast<std::uint64_t>(height - 1U - row) : static_cast<std::uint64_t>(row);
        const auto rowOffset = pitchMagnitude * physicalRow;
        const auto *rowAddress =
            reinterpret_cast<const std::byte *>(decoded.pImage) + static_cast<std::size_t>(rowOffset);
        return RowSpan{rowAddress, static_cast<std::size_t>(pitchMagnitude)};
    }

    std::optional<std::string> checkInvariant(const bool condition, const std::string_view violation)
    {
        return condition ? std::nullopt : std::optional<std::string>{std::in_place, violation};
    }

    std::optional<std::string> checkPluginInfo(const pvdInfoPlugin &info)
    {
        if (auto violation = checkHostString(info.pName, "pvdInfoPlugin.pName is null or unterminated")) {
            return violation;
        }
        if (auto violation = checkHostString(info.pVersion, "pvdInfoPlugin.pVersion is null or unterminated")) {
            return violation;
        }
        return checkHostString(info.pComments, "pvdInfoPlugin.pComments is null or unterminated");
    }

    std::optional<std::string> checkImageInfo(const pvdInfoImage &info)
    {
        if (info.nPages == 0) {
            return "pvdFileOpen returned an image with no pages";
        }
        if (auto violation = checkHostString(info.pFormatName, "pvdInfoImage.pFormatName is null or unterminated")) {
            return violation;
        }
        if (auto violation = checkHostString(info.pCompression, "pvdInfoImage.pCompression is null or unterminated")) {
            return violation;
        }
        return checkHostString(info.pComments, "pvdInfoImage.pComments is null or unterminated");
    }

    std::optional<std::string> checkPageInfo(const pvdInfoPage &info)
    {
        if (info.lWidth == 0) {
            return "pvdInfoPage has zero width";
        }
        if (info.lHeight == 0) {
            return "pvdInfoPage has zero height";
        }
        return std::nullopt;
    }

    std::optional<std::string> checkDecoded(const pvdInfoPage &page, const pvdInfoDecode &decoded)
    {
        if (decoded.pImage == nullptr) {
            return "pvdPageDecode returned a null image";
        }
        if (decoded.nBPP != 24 && decoded.nBPP != 32 && decoded.nBPP != 64) {
            return "pvdPageDecode returned a pixel depth other than 24, 32 or 64";
        }

        const auto signedPitch = static_cast<std::int64_t>(decoded.lImagePitch);
        const auto pitchMagnitude = static_cast<std::uint64_t>(signedPitch < 0 ? -signedPitch : signedPitch);
        const auto rowBytes = static_cast<std::uint64_t>(page.lWidth) * (decoded.nBPP / 8U);
        if (pitchMagnitude < rowBytes) {
            return "pvdPageDecode returned a pitch smaller than one row";
        }
        if (!rowSpan(decoded, page.lHeight, 0).has_value()) {
            return "pvdPageDecode returned an image too large to address";
        }
        return std::nullopt;
    }

    std::optional<std::string> checkProgress(const std::uint32_t step, const std::uint32_t steps)
    {
        if (steps == 0) {
            return "decode callback reported no steps";
        }
        if (step >= steps) {
            return "decode callback step is out of range";
        }
        return std::nullopt;
    }

    [[noreturn]] void invariantViolation(const std::string_view invariant) noexcept
    {
        std::fprintf(stderr, "host-sequence invariant failed: %.*s\n", static_cast<int>(invariant.size()),
                     invariant.data());
        std::fflush(stderr);
        std::abort();
    }

    void enforceInvariant(std::optional<std::string> violation)
    {
        if (violation) {
            invariantViolation(*violation);
        }
    }

    SequenceReport replayHostSequence(pvd::Shim &shim, const std::span<const std::byte> input)
    {
        return replayHostSequence(shim, input, AbortPolicy{});
    }

    SequenceReport replayHostSequence(pvd::Shim &shim, const std::span<const std::byte> input,
                                      const AbortPolicy abortPolicy)
    {
        SequenceReport report{};

        // Every Shim entry point is declared noexcept. An escaping C++ exception would therefore
        // terminate at that boundary, which is the required violation signal; no dead catch belongs here.
        enforceInvariant(checkInvariant(shim.init() == PVD_CURRENT_INTERFACE_VERSION,
                                        "pvdInit returned an unsupported interface version"));

        pvdInfoPlugin pluginInfo{};
        shim.pluginInfo(&pluginInfo);
        enforceInvariant(checkPluginInfo(pluginInfo));

        enforceInvariant(checkInvariant(input.size() <= std::numeric_limits<UINT32>::max(),
                                        "memory input does not fit the PVD buffer-size field"));
        pvdInfoImage imageInfo{};
        void *context = nullptr;
        const auto *head = reinterpret_cast<const BYTE *>(input.data());
        const auto opened =
            shim.fileOpen("memory.bin", 0, head, static_cast<UINT32>(input.size()), &imageInfo, &context);
        if (opened == FALSE) {
            enforceInvariant(checkInvariant(context == nullptr, "pvdFileOpen failed with a live context"));
            shim.exit();
            return report;
        }

        enforceInvariant(checkInvariant(context != nullptr, "pvdFileOpen succeeded without a context"));
        enforceInvariant(checkImageInfo(imageInfo));
        report.opened = true;
        report.pages = imageInfo.nPages;

        const auto pagesToReplay = std::min(imageInfo.nPages, UINT32{4});
        const bool abortEnabled = abortPolicy.modulus != 0 && fnv1a(input) % abortPolicy.modulus == 0;
        for (std::uint32_t pageIndex = 0; pageIndex < pagesToReplay; ++pageIndex) {
            pvdInfoPage pageInfo{};
            enforceInvariant(checkInvariant(shim.pageInfo(context, pageIndex, &pageInfo) != FALSE,
                                            "pvdPageInfo failed for an advertised page"));
            enforceInvariant(checkPageInfo(pageInfo));
            enforceInvariant(checkImageInfo(imageInfo));

            CallbackState callbackState{abortEnabled, abortPolicy.callbackIndex};
            pvdInfoDecodeEx decoded{};
            auto &documented = *reinterpret_cast<pvdInfoDecode *>(&decoded);
            const auto decodeSucceeded =
                shim.pageDecode(context, pageIndex, &documented, progressCallback, &callbackState) != FALSE;
            if (decodeSucceeded) {
                enforceInvariant(
                    checkInvariant(!callbackState.aborted, "pvdPageDecode succeeded after the callback aborted"));
                enforceInvariant(checkDecoded(pageInfo, documented));
                touchImage(pageInfo, documented);
                ++report.decoded;
                shim.pageFree(context, &documented);
            } else if (callbackState.aborted) {
                ++report.aborted;
            } else {
                ++report.failed;
            }
            enforceInvariant(checkImageInfo(imageInfo));
        }

        shim.fileClose(context);
        shim.exit();
        return report;
    }

} // namespace pvdkit::sequence
