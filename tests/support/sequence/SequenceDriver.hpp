#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "pvd/PvdApi.hpp"
#include "pvd/Shim.hpp"

namespace pvdkit::sequence
{

    // This driver validates the pvdkit pvd::Shim output contract, not every layout the PVD SDK
    // permits. In particular, Shim always emits non-null strings and decoded BGR24/BGRA32/BGRA64 pages.

    // A finite scan keeps a malformed Shim result from turning contract validation into an
    // unbounded read; all pvdkit-owned strings are much shorter than this limit.
    inline constexpr std::size_t kMaxStringBytes = 4096;

    using RowSpan = std::span<const std::byte>;

    struct AbortPolicy
    {
        std::uint64_t modulus = 5;
        std::uint32_t callbackIndex = 2;
    };

    struct SequenceReport
    {
        bool opened;
        std::uint32_t pages;
        std::uint32_t decoded;
        std::uint32_t aborted;
        std::uint32_t failed;

        bool operator==(const SequenceReport &) const = default;
    };

    [[nodiscard]] std::optional<RowSpan> rowSpan(const pvdInfoDecode &decoded, std::uint32_t height,
                                                 std::uint32_t row) noexcept;
    [[nodiscard]] std::optional<RowSpan> rowSpan(const pvdInfoDecode &decoded, std::uint32_t height, std::uint32_t row,
                                                 std::size_t maximumBufferBytes) noexcept;
    [[nodiscard]] std::optional<std::string> checkInvariant(bool condition, std::string_view violation);
    [[nodiscard]] std::optional<std::string> checkPluginInfo(const pvdInfoPlugin &info);
    [[nodiscard]] std::optional<std::string> checkImageInfo(const pvdInfoImage &info);
    [[nodiscard]] std::optional<std::string> checkPageInfo(const pvdInfoPage &info);
    [[nodiscard]] std::optional<std::string> checkDecoded(const pvdInfoPage &page, const pvdInfoDecode &decoded);
    [[nodiscard]] std::optional<std::string> checkProgress(std::uint32_t step, std::uint32_t steps);
    [[noreturn]] void invariantViolation(std::string_view invariant) noexcept;
    void enforceInvariant(std::optional<std::string> violation);

    [[nodiscard]] SequenceReport replayHostSequence(pvd::Shim &shim, std::span<const std::byte> input);
    [[nodiscard]] SequenceReport replayHostSequence(pvd::Shim &shim, std::span<const std::byte> input,
                                                    AbortPolicy abortPolicy);

} // namespace pvdkit::sequence
