# RPGMVP plugin design

## Format and detection

An encrypted file contains a 16-byte RPG Maker header, 16 encrypted bytes corresponding to PNG
bytes 0..15, and unmodified PNG bytes 16..end. Those first PNG bytes are invariant: the signature,
the 13-byte IHDR length and the `IHDR` chunk name. Detection requires at least 49 bytes, the exact
eight-byte `RPGMV\0\0\0` signature, and a valid IHDR CRC after substituting the invariant bytes.
Version bytes are informational and are never gated. CRC and recognition live in `src/core/Format`.

## Adapter

`src/adapters/spng` is the only layer that includes libspng or zlib. Its stream callback serves the
16 invariant PNG bytes and then the source span beginning at encrypted offset 32, without assembling
a decrypted PNG. Factory parsing obtains IHDR, tRNS and sRGB signalling and normalizes it into shared
`ImageMeta`. Image and chunk limits are installed before parsing.

Each `decodeFrame()` creates a fresh libspng context over the borrowed mapped span. This makes
repeated decodes and simultaneously outstanding pages independent. `PixelBuffer` guarantees tight
rows, so the adapter uses libspng's simpler whole-image decode directly into the destination rather
than progressive row calls. Normal output selects RGB8 or RGBA8 from normalized alpha. Deep output
for sources over 8 bits selects RGBA16; libspng documents every format except `SPNG_FMT_RAW` as
host-endian, so Windows receives little-endian 16-bit samples. The adapter asks libspng to apply
tRNS and swaps the R/B byte units in place to produce BGR/BGRA. Alpha remains straight. No gamma
decode flag is used: PNG `gAMA` and `cHRM` remain ignored. RPGMVP supplies only sRGB or unspecified
CICP, so shared colour presentation is an identity and its output is unchanged. A Bgra64 request
for an 8-bit-or-shallower source is rejected as Unsupported.

libspng 0.7.4's upstream CMake policy level otherwise ignores `CMAKE_MSVC_RUNTIME_LIBRARY`; the
repository overlay port enables CMP0091 so the existing clang-cl chainload produces `/MT` archives.
It does not patch codec source.

## Deflate implementation: stock zlib (zlib-ng measured and not adopted)

Task 23 measured zlib-ng 2.3.3 in `ZLIB_COMPAT` mode (an overlay port installed under the name
`zlib`, SSE2/SSSE3/SSE4.2/PCLMULQDQ/AVX2/AVX-512 kernels with run-time dispatch confirmed enabled
on both architectures) against vcpkg's stock zlib 1.3.2 on identical inputs with the skipped
timing cases in `tests/adapters/DecodeTimingTests.cpp` (a synthetic 4000x3000 photo-like RGBA8
PNG, 46 % of its raw size, and a 16-bit variant, 54 %; `--no-skip=true` runs them, median of 5
decodes after a warm-up, two runs each, Release, Ryzen 5 5500X3D; the adapter-only figures,
which exclude the page allocation, are the inflate-bound ones and are quoted here - the full
report table is in `docs/tasks/report-task23.md`). x64: 185.9-188.4 ms -> 154.5-158.6 ms for
RGBA8 (1.17-1.22x) and 532.7-535.4 ms -> 452.0-470.5 ms for RGBA16 (1.13-1.18x). x86:
195.7-195.8 ms -> 230.3-232.2 ms for RGBA8 (0.84-0.85x, slower) and 615.8-617.7 ms ->
652.7-654.8 ms for RGBA16 (0.94-0.95x, slower). The adoption bar was 1.3x on x64, so the plugin
stays on stock zlib. The decode is not inflate-bound: libspng's own row handling (unfiltering,
format conversion) and the adapter's R/B swap take the larger share, so a faster inflate moves
the total by a fifth at best. The timing cases stay in the tree as the instrument for any later
comparison (`PVDKIT_RPGMVP_TIMING_CACHE=<dir>` keeps the encoded input identical across builds;
the numbers above were taken with generator version 1 of the 16-bit input, whose alpha low byte
carried noise - version 2 makes it opaque, so a re-measurement starts from a fresh cache).

## Core and composition

`src/core/Describe` derives the public format, compression and comment strings solely from
`ImageMeta`. `DefaultPlugin.cpp` owns the Win32 file source, libspng decoder factory, describer,
the shared `core::colour::SrgbOutputTables` (built once in `pvdInit`; libspng reports identity
CICP, so no session of this plugin presents colour today, but `CodecPlugin` takes the tables by
reference and no function-local static may replace them - ARCHITECTURE §7) and the shared
`CodecPlugin` in dependency order. Its limits and identity match the generated VERSIONINFO.
Deep output is enabled by default: sources deeper than 8 bits are preserved as BGRA64. Sources at
8 bits or below retain the BGR24/BGRA32 layouts.

## Exclusions

Audio encryption variants, key recovery, PNG encoding, APNG animation pages, PNG `gAMA`/`cHRM`
application and ICC profile application are out of scope. ICC profile presence is detected
(`ImageMeta::hasIcc`) but the bytes are not retained or forwarded through the PVD ABI. RPGMVP/PNG has no
EXIF-orientation path. Unknown ancillary chunks remain libspng's responsibility; corrupt critical
data is rejected as an expected parse/decode failure.

## Verification fixtures

Real MV and MZ images cover all colour families present in the supplied games, both extensions,
tRNS, 4/8-bit indexed input, RGB/RGBA 8/16-bit input and Adam7. Synthetic images add greyscale,
greyscale-alpha, 1-bit, sRGB, APNG and hostile-size/truncation paths. The Adam7 file has a
non-interlaced pixel-identical encoding. Two reference encrypted/plain pairs validate the exact
32-byte reconstruction rule. See `fixtures/SOURCES.md` for the recorded values.
