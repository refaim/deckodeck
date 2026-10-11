#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "adapters/exr/Decoder.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Types.hpp"

namespace
{
    using pvdkit::pvd::PixelFormat;
    constexpr pvdkit::core::DecoderOptions kLegacy{4, false, std::uint64_t{16'384} * 16'384, 32'768, true};
    constexpr pvdkit::core::DecoderOptions kNative{4, false, std::uint64_t{16'384} * 16'384, 32'768, true, true};

    std::vector<std::byte> read(const std::filesystem::path &path)
    {
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
        std::ifstream file{path, std::ios::binary};
        REQUIRE(file.good());
        file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.good());
        return bytes;
    }

    std::uint16_t code(const std::span<const std::byte> bytes, const std::size_t offset)
    {
        return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]) |
                                          (std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8U));
    }
} // namespace

TEST_CASE("native EXR output preserves every accepted fixture's PQ pixels and peak with padded rows")
{
    pvdkit::exr::DecoderFactory factory;
    std::size_t accepted = 0;
    for (const auto &entry : std::filesystem::directory_iterator{PVDKIT_FIXTURE_DIR}) {
        if (entry.path().extension() != ".exr") {
            continue;
        }
        const auto bytes = read(entry.path());
        auto legacy = factory.create(bytes, kLegacy);
        if (!legacy) {
            continue;
        }
        ++accepted;
        CAPTURE(entry.path().filename().string());
        auto native = factory.create(bytes, kNative);
        REQUIRE(native.has_value());
        const auto &meta = (*native)->meta();
        CHECK(meta.masteringPeakNits == (*legacy)->meta().masteringPeakNits);
        CHECK(meta.sourceDetail == (*legacy)->meta().sourceDetail);
        CHECK(meta.hasAlpha == (*legacy)->meta().hasAlpha);
        const auto format = meta.hasAlpha ? PixelFormat::Rgba64 : PixelFormat::Rgb48;
        const std::uint32_t stride = meta.hasAlpha ? 8U : 6U;
        const auto pitch = meta.width * stride + 10;
        std::vector<std::byte> rgb(static_cast<std::size_t>(pitch) * meta.height, std::byte{0xAB});
        std::vector<std::byte> bgra(static_cast<std::size_t>(meta.width) * meta.height * 8);
        REQUIRE((*legacy)->decodeFrame(0, PixelFormat::Bgra64, bgra, meta.width * 8).has_value());
        REQUIRE((*native)->decodeFrame(0, format, rgb, pitch).has_value());
        for (std::uint32_t y = 0; y < meta.height; ++y) {
            for (std::uint32_t x = 0; x < meta.width; ++x) {
                const auto left = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * stride;
                const auto right = (static_cast<std::size_t>(y) * meta.width + x) * 8;
                REQUIRE(code(rgb, left) == code(bgra, right + 4));
                REQUIRE(code(rgb, left + 2) == code(bgra, right + 2));
                REQUIRE(code(rgb, left + 4) == code(bgra, right));
                if (meta.hasAlpha) {
                    REQUIRE(code(rgb, left + 6) == code(bgra, right + 6));
                }
            }
            const auto padding = std::span{rgb}.subspan(
                static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(meta.width) * stride, 10);
            CHECK(std::ranges::all_of(padding, [](const auto value) { return value == std::byte{0xAB}; }));
        }
    }
    CHECK(accepted >= 60);
}

TEST_CASE("native EXR destinations enforce selected layout pitch capacity and frame")
{
    pvdkit::exr::DecoderFactory factory;
    for (const std::string_view name : {"rgb_half_zip.exr", "rgba_premultiplied.exr"}) {
        CAPTURE(name);
        const auto bytes = read(std::filesystem::path{PVDKIT_FIXTURE_DIR} / name);
        auto created = factory.create(bytes, kNative);
        REQUIRE(created.has_value());
        const auto &meta = (*created)->meta();
        const auto format = meta.hasAlpha ? PixelFormat::Rgba64 : PixelFormat::Rgb48;
        const std::uint32_t pitch = meta.width * (meta.hasAlpha ? 8U : 6U);
        std::vector<std::byte> out(static_cast<std::size_t>(pitch) * meta.height, std::byte{0xAB});
        REQUIRE((*created)->decodeFrame(0, format, out, pitch).has_value());
        for (const auto invalid :
             {PixelFormat::Bgr24, PixelFormat::Bgra32, PixelFormat::Bgra64, PixelFormat::Rgb24, PixelFormat::Rgba32,
              meta.hasAlpha ? PixelFormat::Rgb48 : PixelFormat::Rgba64, static_cast<PixelFormat>(255)}) {
            const auto rejected = (*created)->decodeFrame(0, invalid, out, pitch);
            REQUIRE_FALSE(rejected.has_value());
            CHECK(rejected.error().code == pvdkit::core::ErrorCode::Internal);
        }
        CHECK((*created)->decodeFrame(0, format, out, pitch - 1).error().code == pvdkit::core::ErrorCode::Internal);
        CHECK((*created)->decodeFrame(0, format, std::span{out}.first(out.size() - 1), pitch).error().code ==
              pvdkit::core::ErrorCode::Internal);
        CHECK((*created)->decodeFrame(1, format, out, pitch).error().code == pvdkit::core::ErrorCode::PageOutOfRange);
    }
}

TEST_CASE("EXR destination sizes widen native row and padded image arithmetic before multiplication")
{
    using pvdkit::core::ErrorCode;
    using pvdkit::exr::detail::checkDestination;
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    for (const auto format : {PixelFormat::Rgb48, PixelFormat::Rgba64, PixelFormat::Bgra64}) {
        CAPTURE(format);
        const std::uint32_t stride = format == PixelFormat::Rgb48 ? 6U : 8U;
        // One pixel beyond the largest representable row: narrowing width * stride would
        // wrap to 2 bytes for RGB48, or 0 bytes for either four-channel layout.
        const pvdkit::exr::Layout wide{maximum / stride + 1, 1, 0, 0, std::nullopt};
        const auto row = checkDestination(wide, format, maximum, maximum, format);
        REQUIRE_FALSE(row.has_value());
        CHECK(row.error().code == ErrorCode::Internal);

        // The row fits, including padding, but two rows exceed this destination capacity.
        // A 32-bit pitch * height would wrap to maximum - 1 and accept the small capacity.
        const pvdkit::exr::Layout padded{maximum / stride, 2, 0, 0, std::nullopt};
        const auto image = checkDestination(padded, format, maximum, maximum, format);
        REQUIRE_FALSE(image.has_value());
        CHECK(image.error().code == ErrorCode::Internal);

        const pvdkit::exr::Layout oneRow{maximum / stride, 1, 0, 0, std::nullopt};
        CHECK(checkDestination(oneRow, format, maximum, maximum, format).has_value());
    }
}
