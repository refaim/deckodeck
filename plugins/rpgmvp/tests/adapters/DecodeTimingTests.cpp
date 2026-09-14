// Release timing of the RPGMVP decode path: the instrument behind the zlib-ng decision recorded
// in DESIGN.md (Task 23; stock zlib stayed). Every case is skipped, so CTest never runs them;
// `rpgmvp_adapter_tests --no-skip=true -tc="RPGMVP release timing*"` does. No fixture file is
// involved: each case synthesises a large photo-like image, encodes it to PNG in memory with
// libspng's encoder, wraps it in the RPG Maker container, and times two things: the production
// plugin driven like the host (open in memory mode, decode page 0, free: the user-visible number,
// page faults of the fresh 48/96 MB page included) and the adapter alone decoding into a buffer
// that already exists (the inflate-bound number). Deflate streams differ between zlib
// implementations, so PVDKIT_RPGMVP_TIMING_CACHE=<dir> keeps the encoded files across builds and
// the before/after numbers describe the same input.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <spng.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "adapters/spng/Decoder.hpp"
#include "core/Format.hpp"
#include "core/IDecoder.hpp"
#include "pvd/Plugin.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/Types.hpp"

namespace
{

    constexpr std::size_t kTimingRuns = 5;
    constexpr std::uint32_t kWidth = 4'000;
    constexpr std::uint32_t kHeight = 3'000;
    constexpr std::size_t kChannels = 4;
    // Photographs are smooth gradients plus a little sensor noise: 0..7 levels added to the 8-bit
    // colour samples (a PNG of such content compresses to roughly half its raw size at libspng's
    // default level), and 0..63 in the low byte of the 16-bit colour samples; alpha is opaque at
    // either depth. kSynthesisVersion names the generator in the cache file name so an input
    // encoded by an earlier generator is never mistaken for this one.
    constexpr std::uint32_t kNoiseMask8 = 0x07U;
    constexpr std::uint32_t kNoiseMaskLow16 = 0x3FU;
    constexpr int kSynthesisVersion = 2;
    constexpr pvdkit::core::DecoderOptions kOptions{1, false, std::uint64_t{16'384} * 16'384, 32'768, true};

    struct FreeDeleter
    {
        void operator()(void *pointer) const noexcept
        {
            std::free(pointer); // libspng hands out malloc'd memory (spng_get_png_buffer)
        }
    };

    /// Deterministic photo-like RGBA samples (xorshift32 noise over three gradients, opaque alpha at
    /// both depths: 0xFF, or 0xFFFF for 16-bit).
    std::vector<std::byte> synthesiseRgba(const std::uint32_t width, const std::uint32_t height,
                                          const std::size_t bytesPerSample)
    {
        std::vector<std::byte> image(static_cast<std::size_t>(width) * height * kChannels * bytesPerSample);
        std::uint32_t state = 0x9E37'79B9U;
        const auto noise = [&state] {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            return state;
        };
        std::size_t offset = 0;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint32_t random = noise();
                const std::array<std::uint32_t, kChannels> samples{
                    ((x * 255U) / width + (random & kNoiseMask8)) & 0xFFU,
                    ((y * 255U) / height + ((random >> 8U) & kNoiseMask8)) & 0xFFU,
                    (((x + y) * 255U) / (width + height) + ((random >> 16U) & kNoiseMask8)) & 0xFFU,
                    0xFFU,
                };
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    // PNG stores 16-bit samples big-endian: the 8-bit value is the high byte, and the
                    // low byte carries noise on the colour channels and 0xFF on alpha (0xFFFF = opaque).
                    image[offset++] = static_cast<std::byte>(samples[channel]);
                    if (bytesPerSample == 2) {
                        const std::uint32_t low = channel == kChannels - 1 ? 0xFFU : (random >> 24U) & kNoiseMaskLow16;
                        image[offset++] = static_cast<std::byte>(low);
                    }
                }
            }
        }
        return image;
    }

    std::vector<std::byte> encodePng(const std::span<const std::byte> image, const std::uint32_t width,
                                     const std::uint32_t height, const std::uint8_t bitDepth)
    {
        const std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)> context{spng_ctx_new(SPNG_CTX_ENCODER),
                                                                          &spng_ctx_free};
        REQUIRE(context != nullptr);
        REQUIRE(spng_set_option(context.get(), SPNG_ENCODE_TO_BUFFER, 1) == SPNG_OK);
        spng_ihdr ihdr{};
        ihdr.width = width;
        ihdr.height = height;
        ihdr.bit_depth = bitDepth;
        ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
        REQUIRE(spng_set_ihdr(context.get(), &ihdr) == SPNG_OK);
        REQUIRE(spng_encode_image(context.get(), image.data(), image.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) ==
                SPNG_OK);
        std::size_t length = 0;
        int error = SPNG_OK;
        const std::unique_ptr<void, FreeDeleter> buffer{spng_get_png_buffer(context.get(), &length, &error)};
        REQUIRE(error == SPNG_OK);
        REQUIRE(buffer != nullptr);
        const std::span png{static_cast<const std::byte *>(buffer.get()), length};
        return {png.begin(), png.end()};
    }

    /// The RPG Maker container: a 16-byte header, PNG bytes 0..15 "encrypted" (any bytes: the
    /// decoder substitutes the invariant signature/IHDR prefix), then the PNG unchanged from byte 16.
    std::vector<std::byte> wrapRpgmvp(const std::span<const std::byte> png)
    {
        constexpr std::array header{std::byte{'R'}, std::byte{'P'}, std::byte{'G'}, std::byte{'M'},
                                    std::byte{'V'}, std::byte{0},   std::byte{0},   std::byte{0},
                                    std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'3'},
                                    std::byte{'0'}, std::byte{'1'}, std::byte{0},   std::byte{0}};
        std::vector<std::byte> file(header.begin(), header.end());
        for (std::size_t index = 0; index < pvdkit::rpgmvp::kPngHeader.size(); ++index) {
            file.push_back(png[index] ^ std::byte{0xA5});
        }
        file.insert(file.end(), png.begin() + static_cast<std::ptrdiff_t>(pvdkit::rpgmvp::kPngHeader.size()),
                    png.end());
        return file;
    }

    std::filesystem::path cacheDirectory()
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        const DWORD length =
            GetEnvironmentVariableW(L"PVDKIT_RPGMVP_TIMING_CACHE", buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return {};
        }
        return std::filesystem::path{std::wstring_view{buffer.data(), length}};
    }

    /// The encoded input, synthesised on demand; reused from PVDKIT_RPGMVP_TIMING_CACHE when that
    /// directory is set so two builds time exactly the same bytes.
    std::vector<std::byte> timingInput(const std::uint8_t bitDepth)
    {
        const auto name = "rpgmvp-timing-" + std::to_string(kWidth) + "x" + std::to_string(kHeight) + "-rgba" +
                          std::to_string(bitDepth) + "-noise" + std::to_string(kNoiseMask8) + "-v" +
                          std::to_string(kSynthesisVersion) + ".rpgmvp";
        std::filesystem::path cached;
        if (const auto directory = cacheDirectory(); !directory.empty()) {
            cached = directory / name;
            if (std::ifstream input{cached, std::ios::binary | std::ios::ate}; input) {
                std::vector<std::byte> bytes(static_cast<std::size_t>(input.tellg()));
                input.seekg(0);
                input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                REQUIRE(input);
                MESSAGE("input: " << cached.string() << " (cached, " << bytes.size() << " bytes)");
                return bytes;
            }
        }
        const auto image = synthesiseRgba(kWidth, kHeight, bitDepth / 8U);
        const auto png = encodePng(image, kWidth, kHeight, bitDepth);
        auto file = wrapRpgmvp(png);
        if (!cached.empty()) {
            std::ofstream output{cached, std::ios::binary};
            output.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
            REQUIRE(output);
            MESSAGE("input: written to " << cached.string());
        }
        return file;
    }

    /// Median of kTimingRuns calls after one warm-up, in milliseconds.
    template <class Body> double medianMilliseconds(const Body &body)
    {
        body();
        std::array<double, kTimingRuns> timings{};
        for (auto &timing : timings) {
            const auto start = std::chrono::steady_clock::now();
            body();
            timing = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
        std::ranges::sort(timings);
        return timings[kTimingRuns / 2];
    }

    /// The host path: open in memory mode, decode page 0 into a fresh page, free it.
    double measureHostDecode(const std::span<const std::byte> file, const std::uint32_t bitsPerPixel)
    {
        const auto plugin = pvdkit::pvd::makePlugin(kOptions);
        REQUIRE(plugin != nullptr);
        return medianMilliseconds([&] {
            auto session = plugin->open(pvdkit::pvd::OpenRequest{"timing.rpgmvp", 0, file});
            REQUIRE(session.has_value());
            auto page = (*session)->decodePage(0, pvdkit::pvd::Progress{});
            REQUIRE(page.has_value());
            CHECK(page->bitsPerPixel == bitsPerPixel);
            CHECK(page->pixels.size() == static_cast<std::size_t>(kWidth) * kHeight * bitsPerPixel / 8U);
            CHECK((*session)->freePage(page->pixels));
        });
    }

    /// The adapter alone (parse, inflate, unfilter, channel swap) into a buffer that exists already.
    double measureAdapterDecode(const std::span<const std::byte> file, const std::uint32_t bitsPerPixel)
    {
        const auto format = bitsPerPixel == 64 ? pvdkit::pvd::PixelFormat::Bgra64 : pvdkit::pvd::PixelFormat::Bgra32;
        const auto pitch = kWidth * bitsPerPixel / 8U;
        std::vector<std::byte> destination(static_cast<std::size_t>(pitch) * kHeight);
        pvdkit::rpgmvp::DecoderFactory factory;
        return medianMilliseconds([&] {
            auto decoder = factory.create(file, kOptions);
            REQUIRE(decoder.has_value());
            REQUIRE((*decoder)->decodeFrame(0, format, destination, pitch));
        });
    }

    void reportTiming(const std::uint8_t bitDepth)
    {
        const auto file = timingInput(bitDepth);
        REQUIRE(pvdkit::rpgmvp::recognises(file));
        const std::uint32_t bitsPerPixel = bitDepth == 16 ? 64U : 32U;
        const auto decodedBytes = static_cast<double>(kWidth) * kHeight * bitsPerPixel / 8.0;
        const auto host = measureHostDecode(file, bitsPerPixel);
        const auto adapter = measureAdapterDecode(file, bitsPerPixel);
        MESSAGE("RPGMVP " << kWidth << "x" << kHeight << " RGBA" << static_cast<int>(bitDepth) << ": compressed "
                          << file.size() << " bytes (" << 100.0 * static_cast<double>(file.size()) / decodedBytes
                          << " % of the decoded page); host path median " << host << " ms ("
                          << decodedBytes / (host * 1'000.0) << " MB/s); adapter into an existing buffer median "
                          << adapter << " ms (" << decodedBytes / (adapter * 1'000.0) << " MB/s); " << kTimingRuns
                          << " runs after one warm-up each; " << pvdkit::rpgmvp::libraryVersions());
    }

} // namespace

TEST_CASE("RPGMVP release timing: 4000x3000 RGBA8" * doctest::skip())
{
    reportTiming(8);
}

TEST_CASE("RPGMVP release timing: 4000x3000 RGBA16" * doctest::skip())
{
    reportTiming(16);
}
