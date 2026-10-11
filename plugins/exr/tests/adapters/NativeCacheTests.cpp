#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <malloc.h>

#include "adapters/exr/Context.hpp"
#include "adapters/exr/Pixels.hpp"
#include "core/Parts.hpp"

namespace pvdkit::exr
{
    TEST_CASE("native opaque EXR cache allocates six bytes per overlap pixel while alpha and legacy allocate eight")
    {
        const core::DecoderOptions options{1, false, std::uint64_t{16'384} * 16'384, 32'768, true};
        const PqCodeTables tables;
        for (const std::string_view name : {"rgb_half_zip.exr", "rgba_premultiplied.exr", "data_inside_display.exr",
                                            "data_disjoint.exr", "tiled_data_offset.exr", "Rec709_YC.exr"}) {
            CAPTURE(name);
            const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
            std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
            std::ifstream file{path, std::ios::binary};
            file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(file.good());
            Stream stream{bytes};
            const auto context = openContext(stream, options);
            REQUIRE(context.has_value());
            const auto parts = readParts(*context, stream);
            REQUIRE(parts.has_value());
            const auto choice = choosePart(*parts);
            REQUIRE(choice.has_value());
            const auto header = readHeader(*context, static_cast<int>(choice->index), stream);
            REQUIRE(header.has_value());
            const auto shape = layout(header->display, header->data, options);
            REQUIRE(shape.has_value());
            const auto pixels =
                shape->overlap ? static_cast<std::size_t>(shape->overlap->width() * shape->overlap->height()) : 0;
            for (const bool native : {false, true}) {
                CAPTURE(native);
                const bool alpha = choice->selection.hasAlpha();
                const auto format =
                    native ? (alpha ? pvd::PixelFormat::Rgba64 : pvd::PixelFormat::Rgb48) : pvd::PixelFormat::Bgra64;
                const EncodeParams params{100.0F, alpha, false, {0.2126F, 0.7152F, 0.0722F}, tables, format};
                const auto cached = readPixels(*context, static_cast<int>(choice->index), stream, *header,
                                               choice->selection, *shape, params);
                REQUIRE(cached.has_value());
                const auto channels = native && !alpha ? 3U : 4U;
                CHECK(cached->codes == pixels * channels);
                CHECK(cached->histogram.count() == pixels);
                if (pixels != 0) {
                    const auto allocation = _msize(cached->pixels.get());
                    CHECK(allocation == pixels * channels * sizeof(std::uint16_t));
                    std::cout << "[cache] " << name << " native=" << native << " overlap=" << pixels
                              << " codes=" << cached->codes << " allocated=" << allocation << '\n';
                } else {
                    CHECK(cached->pixels == nullptr);
                }
            }
        }
    }
} // namespace pvdkit::exr
