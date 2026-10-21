#pragma once

#include <cstddef>
#include <span>

namespace pvdkit::svg
{

    /// Recognises a gzip (possible SVGZ) header or a plausible XML candidate: at least two supplied
    /// bytes, optional UTF-8 BOM and XML whitespace, then '<', or only BOM/whitespace (the root may
    /// lie beyond the supplied head). Anything that is not SVG is rejected by resvg during open.
    [[nodiscard]] bool recognises(std::span<const std::byte> head) noexcept;

    /// True for the two-byte gzip magic.
    [[nodiscard]] bool isGzip(std::span<const std::byte> input) noexcept;

} // namespace pvdkit::svg
