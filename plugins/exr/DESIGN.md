# EXR.pvd — plugin design

The shared design (layers, canonical interfaces, build, gates) is `docs/ARCHITECTURE.md`; this
file holds what is specific to the OpenEXR plugin: which library API it uses and why, how a file
becomes one picture (part, view, channels, windows), the colour path (exposure, primaries, peak,
delivery to the shared presentation), the info line, the fixtures and the v1 exclusions.
Namespace: `pvdkit::exr`. Include root: `plugins/exr/src`. Extensions the host maps: `.exr`, `.sxr`.

The product intent, in Roma's words: the picture must simply look right, with nothing to adjust.
The plugin shows an EXR the way a colour-managed viewer shows it by default: scene-linear float
pixels become a display-ready sRGB picture with the file's primaries honoured and BT.2390 highlight
compression from the picture's own peak, delivered at 16 bits per channel so PictureView's
dithering and 10-bit output keep the precision.

## 1. Library

OpenEXR 3.4.13 from vcpkg (port `openexr`; dependencies `imath` 3.2.2, `libdeflate` 1.25,
`openjph` 0.30.1 for HTJ2K), static, both architectures. Three overlay ports were needed, each a
copy of the stock port plus one build change with its reason in `portfile.cmake`:

- `ports/openjph`: `ojph_block_decoder_ssse3.cpp` uses SSSE3 intrinsics without a compile flag under
  MSVC (cl.exe accepts them regardless; there is no `/arch:SSSE3`), which clang-cl refuses. The patch
  adds `/clang:-mssse3` for that file when the compiler is clang; the kernel is CPUID-dispatched.
- `ports/openexr`: the same for the SSE4.1 byte-reconstruction kernel of the ZIP decoder, selected by
  `_MSC_VER` in `internal_zip.c` (Core) and `ImfZip.cpp` (C++); the patch gives the function a
  `target("ssse3,sse4.1")` attribute under clang. Every x86-64 CPU since 2008 has SSE4.1, which is
  what the unpatched MSVC build assumes as well.
- `ports/imath`: the stock port with `IMATH_HALF_USE_LOOKUP_TABLE=OFF`, so that no consumer of
  `half.h` references the half-to-float table `half.cpp.obj` defines (that object drags the CRT's
  `thread_safe_statics.obj` into a DLL, rule 13; the paragraph on the C++ library below). The
  conversion becomes the bit-exact integer arithmetic in the header; a rebuild of Imath and OpenEXR.

The four ports declare `cmake_minimum_required` below 3.15, so their CMake ignored the chainload
toolchain's `CMAKE_MSVC_RUNTIME_LIBRARY` and built `/MD` (lld-link refused the mix with
`/failifmismatch RuntimeLibrary`). Both triplets now pass `-DCMAKE_POLICY_DEFAULT_CMP0091=NEW` to
every port (`VCPKG_CMAKE_CONFIGURE_OPTIONS`), which the libspng overlay port had done privately;
every port rebuilt once (ABI hash change), the existing plugins' DLLs are unaffected.

**Which API.** The plugin talks to the library exclusively through **OpenEXRCore**, the C API
underneath the C++ classes: a read context over the file bytes (`exr_start_read` with `read_fn` /
`size_fn` over the span - no file name reaches the library), the header queries (`exr_get_count`,
`exr_get_storage`, `exr_get_channels`, `exr_get_data_window`, `exr_get_display_window`,
`exr_get_lineorder`, `exr_get_compression`, `exr_get_tile_descriptor`, `exr_get_tile_levels`,
`exr_attr_get_chromaticities`, `exr_attr_get_float`, `exr_attr_get_string`,
`exr_attr_get_string_vector`, `exr_get_name`) and the chunk pipeline (`exr_read_scanline_chunk_info` /
`exr_read_tile_chunk_info`, `exr_decoding_initialize` / `_update` / `_choose_default_routines` /
`_run` / `_destroy`). The reasons are the rules, not taste:

- The C++ layer (`Imf::InputFile`, `RgbaInputFile`, ...) reports every expected failure - a corrupt
  chunk, a truncated file - by throwing `Iex` exceptions, and no adapter may catch (AGENTS.md rule 5;
  the guard test forbids `catch (` outside `Firewall.hpp`). The Core reports by return code.
- The C++ layer's thread pool (`IlmThread::ThreadPool::globalThreadPool()`) is a function-local
  static with a constructor; on MSVC ≥ 14.50 its guarded initialisation imports
  `api-ms-win-core-synch-l1-2-0.dll` (rule 13, ARCHITECTURE §7). The DLL references no IlmThread
  symbol at all.
- Exceptions from OpenJPH (HTJ2K) never reach us: the Core wraps every OpenJPH call in its own
  try/catch (`internal_ht.cpp`) and answers `EXR_ERR_CORRUPT_CHUNK`.

One piece of the C++ library is linked: four `Imf::RgbaYca` free functions
(`reconstructChromaHoriz`, `reconstructChromaVert`, `YCAtoRGBA`, `fixSaturation`), arithmetic over
half `Rgba` rows that throws nothing, for the luminance/chroma files (§5). `RgbaYca::computeYw` is
*not* used: it throws `std::invalid_argument` on a white with y = 0 and on collinear primaries
(`Imf::RGBtoXYZ`), which a hostile file can carry; the weights it would compute are spelled out in
`LuminanceChroma.cpp` over a set the plugin validated first (§4). The linker pulls
`ImfRgbaYca.obj` out of `OpenEXR-3_4.lib` and nothing else of it - no `ImfChromaticities.obj`,
no Iex, no IlmThread - and nothing of Imath: `half.cpp.obj`, which defines the half-to-float
lookup table and, through `<iostream>`, three `std::locale::id` initialisers whose guards pull the
CRT's `thread_safe_statics.obj` (the object that imports the synch API set on MSVC ≥ 14.50), is
never referenced because the `ports/imath` overlay builds Imath and OpenEXR with
`IMATH_HALF_USE_LOOKUP_TABLE=OFF` and the adapter is compiled with `IMATH_HALF_NO_LOOKUP_TABLE`:
every half-to-float conversion is the bit-exact integer arithmetic in `half.h`. Proof: the
release DLLs re-linked with `/MAP` list neither `half.cpp.obj` nor `thread_safe_statics.obj` on
x64 or x86 (the task report has the maps); the release import table is `KERNEL32.dll` and the
export table the eight names (`exr_check_imports`, `exr_check_exports`).

**Threading: option (b), single-threaded decode in the library.** The Core decodes on the calling
thread; there is no pool to size, and `DecoderOptions::maxThreads` only budgets the shared
presentation's row bands (four at most). A 4K decode is measured in §8; the CI import gate on
MSVC 14.51 is the final proof that nothing from the synch API set is imported.

**Per-context limits, no globals.** `exr_set_default_maximum_image_size` and
`exr_set_default_maximum_tile_size` are process-wide and are never called. The context initializer
carries `max_image_width/height = DecoderOptions::maxDimension` and `max_tile_width/height = 4096`
(the tile scratch is one tile of float RGBA), and the plugin validates the windows itself (§4)
before any allocation. The Core's own size rejections ("too large", "exceeds max") are mapped to
`TooLarge`; `EXR_ERR_FEATURE_NOT_IMPLEMENTED` to `UnsupportedFeature`; everything else to
`ParseFailed` (header) or `DecodeFailed` (chunks), with the Core's message appended. The message
arrives through the context's error handler, a noexcept C callback: it copies into a fixed
512-byte buffer of the `Stream` the context was opened with (no allocation; the Core's own
formatting buffer is 256 bytes, longer messages are cut), and it tolerates the null context the
Core passes for allocation failures and bad arguments.
`EXR_ERR_OUT_OF_MEMORY` is *not* an exception: the Core answers it for hostile data too (a corrupt
deflate stream inflating past the chunk size, `compression.c`), so it is an expected failure of the
file; a real allocation failure surfaces as `std::bad_alloc` from our own containers.

## 2. Composition root (`src/DefaultPlugin.cpp`)

`pvd::makePlugin()` owns, in declaration order, `win::FileSource`, `exr::DecoderFactory`,
`exr::Describer`, `core::colour::SrgbOutputTables` (every EXR session presents colour, so every
session borrows them; built once in `pvdInit`) and `core::CodecPlugin`. Options:
`maxThreads = max(1, hardware_concurrency())`, `strict = false`, `maxPixels = 16384 × 16384`,
`maxDimension = 32768`, `deepOutput = true` (moot: every EXR page is BGRA64 anyway).
`PluginInfo{10, "EXR", "1.0.0", "OpenEXR decoder: OpenEXR 3.4.13, Imath 3.2.2, libdeflate 1.25,
OpenJPH 0.30.1; static build"}` - the versions at run time from `exr_get_library_version` and the
three version headers; the VERSIONINFO comments say the same from the ports vcpkg installed, and
the e2e version test pins the two equal.

## 3. What becomes the picture (`src/core`)

**Recognition** (`DecoderFactory::recognises`): the magic number `76 2f 31 01`, format version 2 in
the fifth byte, and a head of at least 16 bytes (no valid file is shorter; PictureView hands over
at least 16 KiB or the whole file).

**Part and view** (`core/Parts`): every part's storage kind, channel list, `view` attribute and the
single-part `multiView` attribute are read; deep parts (`deepscanline`, `deeptile`) are skipped;
among the parts whose channels yield a selection, the one with `view == "left"` wins, otherwise
the first. Nothing displayable is `UnsupportedFeature` with the reason ("only deep parts, which
this plugin does not show" / "no part carries colour channels").

**Channels** (`core/Channels`), in this order, the first that fits:

1. top-level `R`, `G`, `B` (+ `A`) → RGB(A);
2. `Y` + `RY` + `BY` with `RY`/`BY` sampled 2×2 (+ `A`) → luminance/chroma, reconstructed to RGB
   with the library's `RgbaYca` filters (§5);
3. `Y` alone (+ `A`) → grey;
4. the first layer, by name, that has `layer.R`, `layer.G`, `layer.B` (+ `layer.A`) → RGB(A);
5. the first channel that is usable → grey ("`Z` as grey").

UINT channels (ids, masks) and channels whose sampling is not what the layout expects count as
absent. In a single-part stereo file whose first `multiView` entry is not `left` and which carries a
complete `left.R/G/B` layer, that layer wins over the bare channels. HALF and FLOAT channels are
read as float (the Core converts); the sample type of the first colour channel is reported as
`depth` 16 or 32 (informational; `pvdInfoPage.nBPP` is depth × channels).

**Alpha** is associated (premultiplied) by OpenEXR convention and is handed over straight: colour is
divided by `min(A, 1)` where A > 0 (zero coverage keeps its colour - additive light), alpha is
clamped to [0, 1]; a NaN or negative alpha is transparent, +inf is opaque. `hasAlpha` when an `A`
channel of the chosen level exists.

**Windows** (`core/Windows`, `core/Composite`): the picture is the **display window**; the data
window is composited into it. Both windows are validated first: inverted or empty → `ParseFailed`;
a display window whose side exceeds `maxDimension` or whose area exceeds `maxPixels`, or a data
window whose side exceeds `maxDimension` (it sizes the chunk scratch) → `TooLarge`, all before any
allocation. Two rules for the data window's *area*: RGB and tiled parts are read one chunk at a
time and cropped, so their area is not bounded (a huge data window behind a small display window
costs only the chunks touching the overlap); a luminance/chroma part is reconstructed over its
whole data window (§5), so its area is held to `maxPixels` as well (`checkDataArea`, `TooLarge`
before any allocation; the hostile fixture `yc_huge_data.exr`, a 20000 × 20000 data window behind
a 1 × 1 display window, is refused this way). The overlap
(display ∩ data) is what gets decoded and kept; pixels of the display window outside it are
transparent black when the picture has alpha and opaque black otherwise; data outside the display
window is never read past the chunk it shares. Line order is the Core's business
(`exr_read_scanline_chunk_info` finds a chunk by its row whatever the order); tiled files are read
at level (0, 0) of any mip/rip layout; scanline chunks are walked aligned to the data window's
first row.

## 4. Colour

The scene-linear values go through the *shared* presentation, not a private one. The decoder turns
every pixel into **16-bit PQ codes of absolute nits** (BGRA64, `PqCodeTables`, below) and reports
`cicp = {primaries code, 16 (PQ), 0, full range}` with the tone-mapping peak in
`masteringPeakNits`; `core::colour::Presentation` then does PQ → linear → primaries → BT.2390 →
exact sRGB → 16-bit, the same path AVIF HDR takes, with its zscale-validated maths. In detail:

1. **Primaries.** The `chromaticities` attribute (Rec.709/D65 when absent) is first checked with
   `Primaries::isUsable`: every coordinate finite, the white's y positive, the primaries not
   collinear, the derived matrices finite. A set that fails - the two the library's own
   `RGBtoXYZ` throws on (a white with y = 0, collinear primaries) and anything non-finite - is
   never handed to any matrix: the picture is shown as Rec.709 and the info line says so with the
   attribute's values (`linear Rec.709 assumed, unusable chromaticities (R ... W ...)`); the
   fixtures `rgb_whitey0`, `yc_whitey0`, `yc_collinear` pin it, and the e2e exception counter
   proves nothing throws. A usable set is matched with a tolerance of 1e-3 per coordinate to
   Rec.709 (H.273 code 1), Rec.2020 (9), P3-D65 (12), ACES AP0 and ACES AP1 (ST 2065-1; no H.273
   code) and CIE XYZ (the primaries at the XYZ axes with the equal-energy white, OpenEXR's
   convention for XYZ files; no code). The coded sets go through the presentation's constexpr
   tables; AP0, AP1, XYZ and anything else go through the run-time path: `ImageMeta::chromaticities`
   carries the file's own xy values, `cicp.primaries` says 2 (unspecified), and
   `Primaries::toSrgb(const Chromaticities&)` builds RGB → XYZ → Bradford (white → D65) → BT.709 at
   session construction (`Presentation::needed` is true whenever chromaticities are set). That
   run-time RGB → XYZ is the column form (`Imf::RGBtoXYZ`, Poynton): each primary's (x, y, 1-x-y)
   scaled so that RGB (1, 1, 1) is the white at Y = 1, nothing divided by a primary's y - the XYZ
   set has two primaries at y = 0. It is a derivation of its own next to the compile-time one
   (which divides by y and stays to the bit: the coded tables are pinned by the AVIF hashes). The
   Bradford step is skipped for the equal-energy white E (within 1e-3): CIE XYZ and the other
   colorimetric encodings declare no viewing illuminant with it, their values are absolute
   tristimulus, so a D65-white picture stored as XYZ stays D65 white - what `exrdisplay` does with
   the corpus pair `Rec709_YC` / `XYZ_YC`, and what the e2e test pins (the two present alike: mean
   difference 80 of 65535 codes, the outliers clipped reds where chroma was subsampled in XYZ
   rather than RGB). The 3.4 `colorInteropID` attribute is read for the info line and is the
   fallback when `chromaticities` is absent (`lin_rec709`, `lin_rec2020`, `lin_p3d65`, `lin_ap0`,
   `lin_ap1`; any other id is only reported). The luminance weights for the peak (below) are the
   Y row of the file's own RGB → XYZ.
2. **Exposure.** EXR is scene-linear with no brightness of its own: 1.0 maps to `whiteLuminance`
   nits when the attribute is present, finite and positive, otherwise to **100 nit** (the sRGB view
   convention: 0.18 → 18 nit). NaN, negative and -inf samples are 0; +inf is the PQ peak.
3. **Peak.** The 99.99th-percentile luminance of the display window's pixels (those outside the
   data window count as black), read from a 65,536-bin histogram of the PQ code of each pixel's
   luminance (the histogram's bin edge is within one 16-bit code of the exact percentile, ≈ 0.1 % in
   nits at the dark end), clamped to [100, 10000] nit. Below or at 100 nit the BT.2390 knee is
   inactive ("SDR range, no highlight compression" - only the curve's display-black lift to
   0.005 nit remains, which is why black comes out as sRGB code 42, as in the HLG tests).
4. **Precision.** 16-bit PQ over 0..10000 nit: the relative luminance step per code is ≈ 0.015 % at
   100 nit, ≈ 0.013 % at 5000 nit and grows towards the dark end, ≈ 0.05 % at 0.1 nit and ≈ 0.1 % at
   0.01 nit (worst case in the range a display can show); every step is far below what an 8- or
   10-bit sRGB display can resolve (a 10-bit code step is ≈ 0.4 % of linear light in the mid-tones).
   Half-float sources have a relative step of 0.05 % themselves; FLOAT sources keep their precision
   up to the PQ quantisation.
5. **The encoder** (`src/core/PqTables`, `src/core/Encode`). The code of a luminance is *defined*
   by `pqCode`: the ST 2084 inverse EOTF evaluated in double precision, `lround`ed to 16 bits (the
   shared `Transfer::nitsToPq` is a float curve for decoding; as a definition of the code it is
   noisy at the last bit, since `pow(base, 78.84)` amplifies the base's rounding to half a code,
   and it is not even monotone at code resolution). Two `pow` calls per sample made a 4K decode
   take a second, so the pixel loop never evaluates the curve: `PqCodeTables` precomputes the
   65,535 *decision thresholds* (the first float mapped to each code, from the double-precision
   inverse of the boundary, nudged by an ULP where rounding put it on the wrong side) and encodes
   by counting how many thresholds a value passes. A bucket table indexed by the float's bit
   pattern (512 buckets per octave, 256 KiB) gives the count below the bucket, and a fixed window
   of 16 comparisons from there (no bucket holds more than 14 thresholds; the array is padded with
   +inf) adds the rest - no data-dependent branch, so random pixel values cost what a ramp costs
   (≈ 6 ns per sample on the x64 test machine against ≈ 45 ns for the double `pow` path and
   ≈ 27 ns for a bucketed binary search, which mispredicts its way through real data). The tables
   are built once per process in `pvdInit` (a factory member, no function-local static) and
   borrowed by every decode. Exactness is proven, not argued: the skipped diagnostic case in
   `PqTablesTests` compares the tables with `pqCode` for **every float in [0, 10000]**
   (1,176,256,513 values, 0 mismatches, both architectures), and the regular cases check the
   edges, a million random samples and the neighbourhood of every seventh threshold. The adapter
   tests compare the decoded PQ codes with the double-precision reference within one code (float
   against double at a code boundary), the exhaustive proof is what makes the encoder exact.
6. Grey files (`Y` alone, or a single channel) take the same path with R = G = B.

**Decode at open.** The peak needs every pixel before `pvdFileOpen` returns (the describer prints
it and the presentation is built from it), so `DecoderFactory::create` reads and encodes the whole
overlap: one chunk of float RGBA scratch at a time (scanlines: data width × lines per chunk; tiles:
one tile; luminance/chroma parts: the whole data window, §5), encoded straight into a cached
BGRA64 buffer (8 bytes per overlap pixel, at most `maxPixels`) with the histogram accumulated on
the way. `decodeFrame` composites the cache into the
display window (`checkDestination` refuses anything but BGRA64 with room for the rows). The
library context is closed before `create` returns; the file bytes are no longer needed afterwards
(FileSession keeps them anyway). Cost: the session holds the overlap twice while a page is out
(cache + page), 66 MB + 66 MB for a 4K picture.

## 5. Luminance/chroma files

The Core hands over `Y` at full resolution and `RY`/`BY` at every even row and column (the
`x_samples`/`y_samples` of the chunk's channel info; the Core validated that the data window
starts on even coordinates) into half `Rgba` rows padded by 13 pixels on each side
(`adapters/exr/LuminanceChroma`). The reconstruction then mirrors `Imf::RgbaInputFile::FromYca`
line for line with the library's own functions: `reconstructChromaHoriz` on even rows (pads repeat
the first pixel and the last even one, as `padTmpBuf` does), `reconstructChromaVert` over the 27
rows around each odd row (rows above the top repeat the first row, rows below the bottom the
second-to-last, as `readYCAScanLine` clamps), `YCAtoRGBA` with the luminance weights of the file's
chromaticities (`luminanceWeights`: `computeYw`'s arithmetic spelled out, expression for
expression, so the weights are the library's bits and the reconstruction rounds the halves the
library rounds - the adapter test pins the weights to `computeYw`'s for Rec.709, XYZ and AP0 -
over a set §4 validated, since `computeYw` itself throws on a degenerate one), then
`fixSaturation` over each row and its neighbours. The adapter test compares every pixel of the
four Y/RY/BY fixtures the library can read with `Imf::RgbaInputFile` (linked into the test only):
the worst difference is one 16-bit PQ code. Half precision is what these files carry.

This path reads the whole data window before anything is encoded (the vertical filter needs 27
rows and the saturation fix three, and the library's own reader does the same): the padded half
rows, the reconstructed rows and the float result are about 40 bytes per data-window pixel while
`create` runs, released before it returns. That is why the data window's area is bounded for
these parts (§3, `checkDataArea`); the RGB and tiled paths never hold more than one chunk.

## 6. Describer (`src/core/Describe`) and the shared meta

`ImageMeta` gained three fields for this plugin (docs/ARCHITECTURE.md §3.6): `chromaticities`
(the explicit set, §4), `compression` (the scheme's name; the host's compression field) and
`sourceDetail` (the source half of the comments line, formatted by `exr::describeSource` from the
header facts the adapter collected, because the shared fields cannot say "tiled 64x64" or
"2 parts"). AVIF and RPGMVP leave the strings empty. `Describer` returns
`{"OpenEXR", meta.compression, meta.sourceDetail + ", " + presentationNote(meta)}`, for example:

`half RGBA, linear Rec.709, ZIP, scanline, display 1920x1080, data 1920x1080 at 0,0, 100 nit white, → sRGB (BT.2390 tone map from 1250 nit)`

with, when applicable, `float RGB`, `half Y+chroma`, `half Y with alpha`, `float Z as grey`,
`half RGBA (layer beauty)`; `linear ACES AP0`; `linear, unknown chromaticities (custom: R 0.7347,0.2653 G ... W ...)`;
`colorInteropID lin_ap0`; `tiled 64x64, 5 mip levels` / `5x4 rip levels`; `display 481x371 at -40,-40`
(the origin only when it is not 0,0; the data window always prints its origin); `250 nit white`;
`2 parts (part 1: beauty)`; `stereo (left view)`; `deep parts skipped`; and
`→ sRGB (SDR range, no highlight compression)` at or below 100 nit.

## 7. Build specifics

- vcpkg feature `exr` in `vcpkg.json`: `openexr` (pulls `imath`, `libdeflate`, `openjph`).
- `plugins/exr/CMakeLists.txt`: `exr_core` (Channels, Parts, Windows, Composite, Encode, Colour,
  Describe - no library), `exr_adapter` (Context, Pixels, LuminanceChroma, Decoder; links
  `OpenEXR::OpenEXRCore`, `OpenEXR::OpenEXR` for RgbaYca, `Imath::Imath`,
  `libdeflate::libdeflate_static`, `openjph`), `exr_composition`, then `pvdkit_add_plugin(exr ...)`
  with the license ports `openexr`, `imath`, `libdeflate`, `openjph`.
- Tests: `exr_core_tests` (channel selection, part choice, window layout, compositing, encoding and
  the peak, chromaticity matching, the describer), `exr_adapter_tests` (every fixture's meta and info
  line, PQ codes at seven positions of every reference fixture against the double-precision
  reference, luminance/chroma against `Imf::RgbaInputFile`, rejections and their categories, the
  limits, the composed plugin, the unit-level leak gate), `exr_e2e_tests` (`LoadLibraryW(EXR.pvd)`
  driven like the host on every fixture in both modes, host pixels against the reference pipeline,
  pinned whole-image hashes, callback abort, out-of-range pages, two files at once, rejections,
  after `pvdExit`, four threads, the skipped 4K timing case), `exr_sequence_tests`,
  `exr_leak_tests` with the hostile corpus, `exr_check_imports`, `exr_check_exports`,
  `exr_package_docs`. The C++ OpenEXR library is linked into the adapter and e2e tests only (as
  the reference reader and the 4K generator), never into the plugin.
- ASan preset: OpenEXR is the first C++ port, and its uninstrumented objects carry the MSVC
  STL's `annotate_string`/`annotate_vector` = 0 mark that lld-link refuses to pair with the
  instrumented 1; the top-level `CMakeLists.txt` therefore compiles the preset with
  `_DISABLE_STL_ANNOTATION` (docs/ARCHITECTURE.md, the asan paragraph).
- The guard's codec-header rule covers `OpenEXR/*.h`, `Imath/*.h`, `libdeflate.h` and
  `openjph/*.h`.

## 8. Fixtures and measurements

`fixtures/SOURCES.md` lists every file with origin, license, what it exercises, the verified
expectation and its SHA-256: 19 files from `openexr-images` (DisplayWindow t01/t02/t05/t07/t09/
t13/t14, AllHalfValues, BrightRingsNanInf, GammaChart, GrayRampsHorizontal, WideColorGamut,
WideFloatRange, stripes, Garden, ColorCodedLevels, PeriodicPattern, Rec709_YC, XYZ_YC; pinned
commit and checksums in `scripts/fetch-fixtures.ps1` / `scripts/openexr-images.sha256`) and 53
synthetic ones from `scripts/make-synthetic-fixtures.ps1` (every compression scheme, RGBA
premultiplied, Y float, layers, UINT-only, mixed types, the window cases, decreasing line order,
whiteLuminance, AP0/AP1/2020/P3/custom chromaticities, colorInteropID, a peak beyond 10000 nit,
NaN/inf/negative pixels, oversized display windows and a tile beyond the 4096 limit, truncated and
corrupt files, multi-part files with deep scanline and deep tiled parts, multi-view files and a
`multiView` attribute of the wrong type, tiles, and Y/RY/BY files of their own: one line per chunk,
one with an extra channel, one without BY, one corrupt). The reference method is described there;
the generator runs clang-format over the header it writes so the lint gate has nothing to say.

**4K timing** (`exr_e2e_tests --no-skip=true -tc="4K timing*"`: a 3840 × 2160 half RGB scene
written into %TEMP% with `Imf::RgbaOutputFile`, viewed through the release DLL from disk as the
host does - `pvdFileOpen` + `pvdPageDecode` + `pvdPageFree` + `pvdFileClose` - six times, first
view and the mean of the five others; Release, one process, nothing else running):

| build | ZIP, 26.4 MB | of which open / decode | PIZ, 24.3 MB | of which open / decode |
|---|---|---|---|---|
| x64 | ≈ 650 ms | ≈ 245-250 / ≈ 395 ms | ≈ 680 ms | ≈ 270-280 / ≈ 395 ms |
| x86 | ≈ 930 ms | ≈ 300 / ≈ 625 ms | ≈ 1025 ms | ≈ 385 / ≈ 630 ms |

"open" is the plugin's own work (the Core's decompression, ≈ 100 ms for ZIP and ≈ 130 ms for
PIZ, plus the PQ encoding and the histogram of 8.3 Mpx); "decode" is the shared presentation's
PQ → BT.2390 → sRGB conversion in `core::colour::Presentation::applyImage` (up to four row
bands), the same cost an AVIF HDR picture of that size pays, and the larger half. The run-to-run
spread is the machine's; the numbers are a scale, not a benchmark. The release DLL is 1,110,016
bytes on x64 and 1,002,496 bytes on x86.

## 9. Out of scope (documented, not implemented)

Deep images (parts are skipped, deep-only files refused); multi-part files as pages (the core's
frame mechanism is for equal-size frames of one picture; a documented future extension); the right
view and other views; layers other than the first complete one; UINT-only channels; mip/rip levels
other than the first; the `preview` attribute; pixel aspect ratio (`t15`/`t16`: the host shows
square pixels); ICC profiles are not a thing in EXR; user exposure or tone controls by design.
