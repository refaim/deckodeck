# AVIF.pvd — plugin design

The shared design (layers, canonical interfaces, build, gates) is `docs/ARCHITECTURE.md`; this
file holds what is specific to the AVIF plugin: how the libavif adapter maps onto the shared
`IDecoder` contract, what the describer prints, the composition root's choices, the fixtures and
the v1 exclusions. Namespace: `pvdkit::avif`. Include root: `plugins/avif/src`.

## 1. Composition root (`src/DefaultPlugin.cpp`)

`pvd::makePlugin()` returns an object that owns, in declaration order, `win::FileSource`,
`avif::DecoderFactory`, `avif::Describer` and `core::CodecPlugin`, and forwards `IPlugin`.
Options: `maxThreads = max(1, hardware_concurrency())`, `strict = false`,
`maxPixels = 16384 × 16384`, `maxDimension = 32768`.
`deepOutput = true`: sources deeper than 8 bits are delivered as BGRA64 by default.
`PluginInfo{kPluginIdentity.priority (10), name ("AVIF"), version ("<version>"),
"AVIF decoder: libavif <ver>, dav1d <ver>, libyuv <ver>; static build"}` — the identity from the
generated `pvd/PluginConstants.hpp` (declared once in `plugins/avif/CMakeLists.txt`), the library
versions from `avifVersion()`, `dav1d_version()`, `LIBYUV_VERSION` at run time. The VERSIONINFO
resource carries the same comments text computed by CMake from the installed ports, and the e2e
version test pins the two equal.

## 2. Describer (`src/core/Describe.hpp/.cpp`)

`std::string describe(const core::ImageMeta&)`. Format:
`"<depth>-bit YUV 4:2:0 (limited range), CICP 1/13/6, straight alpha, 12 frames, ICC, EXIF, XMP, clap, irot 1, imir top-bottom"`.
Items absent are omitted; `Yuv400` prints `YUV 4:0:0 (monochrome)`; premultiplied prints
`premultiplied alpha`; still image prints no frames item; full range prints `full range`.
Well-known CICP triples get a suffix: `1/13/6` → `(sRGB)`, `1/1/1` → `(BT.709)`,
`9/16/9` → `(BT.2020 PQ)`, `9/18/9` → `(BT.2020 HLG)`, `12/16/12` → `(P3 PQ)`; `2/2/2` → `(unspecified)`.

`class Describer final : core::IImageDescriber` returns
`ImageDescription{"AVIF", "AV1", describe(meta)}`; `CodecPlugin` turns that into the host's
`ImageInfo` together with `frameCount` / `animated` from the meta.

## 3. Adapter (`src/adapters/avif/Decoder.hpp/.cpp`)

- `avif::Decoder : core::IDecoder`: `std::unique_ptr<avifDecoder, DecoderDestroy>`; constructed
  only by `DecoderFactory` (passkey) from a handle whose `avifDecoderParse` already succeeded.
- `DecoderFactory::recognises` = `avifPeekCompatibleFileType` (returns false for short spans).
- `DecoderFactory::create()`: `avifDecoderCreate`, set `maxThreads`,
  `strictFlags = strict ? AVIF_STRICT_ENABLED : AVIF_STRICT_DISABLED`, `imageSizeLimit`
  (clamped to `[1, AVIF_DEFAULT_IMAGE_SIZE_LIMIT]`, libavif rejects anything else; the real
  `maxPixels` limit is enforced by `PixelBuffer::create` in the shared core), `imageDimensionLimit`,
  `avifDecoderSetIOMemory`, `avifDecoderParse`, then fill `ImageMeta` from `decoder->image`
  (width/height/depth/yuvFormat/yuvRange/colorPrimaries/transferCharacteristics/matrixCoefficients,
  `alphaPresent`, `alphaPremultiplied`, `imageCount`, `transformFlags` + `clap` (via
  `avifCropRectFromCleanApertureBox`; invalid → `InvalidTransform`), `irot.angle`, `imir.axis`
  (libavif's `avifImageMirror` comment: axis 0 exchanges top/bottom → `MirrorAxis::TopBottom`,
  axis 1 → `LeftRight`), `icc.size`, `exif.size`, `xmp.size`). Maps `avifResult` to `ErrorCode`
  with `avifResultToString` in `detail` (a table in `Decoder.cpp`; `AVIF_RESULT_NO_IMAGES_REMAINING`
  → `PageOutOfRange`, `NOT_IMPLEMENTED` / `UNSUPPORTED_DEPTH` → `UnsupportedFeature`, size-limit
  diagnostics "dimensions are too large" → `TooLarge`; re-check the cited `read.c` lines on every
  libavif upgrade).
- The `file` span given to `create` must outlive the decoder (`avifDecoderSetIOMemory` installs a
  persistent memory reader); `core::FileSession` guarantees this by declaring the file data before
  the decoder.
- `frameTiming`: `avifDecoderNthImageTiming` → `llround(duration × 1000)` clamped to `uint32`.
- `decodeFrame`: `avifDecoderNthImage`; `avifRGBImageSetDefaults`; `depth = 16` and `format = BGRA`
  for BGRA64, otherwise the unchanged `depth = 8` and `format = BGRA or BGR`;
  `alphaPremultiplied = AVIF_FALSE` (straight alpha out);
  `chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC`; `pixels = dst.data()`, `rowBytes = pitch`;
  `avifImageYUVToRGB`. RGB samples are full-range unsigned 16-bit values. libavif normalizes the
  source YUV range, applies its colour matrix, clamps to [0,1], and rounds after multiplication by
  65535; alpha is rescaled as `round(alpha * 65535 / ((1 << sourceDepth) - 1))`, or filled with
  65535 when absent. No allocation inside the adapter; `detail::checkDestination` refuses a
  destination too small for `height` rows of `pitchBytes` as `Internal`.
- Grid images, progressive files, 10/12-bit sources, image sequences: handled by libavif; we only
  pass through. HDR (PQ/HLG) is **not** tone-mapped in v1: colours are converted by matrix only.
  ICC profiles are ignored (the PVD interface has no colour management), as in every bundled decoder.
- Transformative properties: the adapter only reports `clap` / `irot` / `imir`; the shared
  `core::Transform` applies them **clap → irot → imir** (AVIF spec §"Transformative properties";
  cross-check the comment on `transformFlags` in the installed `avif/avif.h`).

## 4. Build specifics

- vcpkg feature `avif` in `vcpkg.json`: `libavif[dav1d]` (pulls `libyuv`, which pulls
  `libjpeg-turbo`). The overlay port `ports/libavif` (registered through `VCPKG_OVERLAY_PORTS` in
  the presets) is a copy of the vcpkg port plus one patch: libavif 1.4.2's
  `merge_static_libs.cmake` tests the Clang compiler id before `MSVC` and mis-detects clang-cl; the
  patch checks `MSVC` first so the merge uses the lib.exe-style bundling (`CMAKE_LIBTOOL` /
  llvm-lib). The chainload toolchain must **not** replace `CMAKE_AR` for the same reason.
- `plugins/avif/CMakeLists.txt`: `avif_core` (Describe), `avif_adapter` (links `avif`, `yuv`),
  `avif_composition` (DefaultPlugin.cpp), then `pvdkit_add_plugin(avif ...)` with the licence
  ports `libavif`, `dav1d`, `libyuv` for `LICENSES.txt`, and `package/README.txt.in` for the zip.
- Tests: `avif_core_tests` (Describe, Describer), `avif_adapter_tests` (Decoder on the fixtures,
  the static link of libavif+dav1d, the composed production plugin), `avif_e2e_tests`
  (`LoadLibraryW(AVIF.pvd)` on every fixture in disk and memory mode, exact pixel checks on the
  synthetic fixtures, animation timing, rotation, callback abort, rejection of non-AVIF input,
  behaviour after `pvdExit`, four threads decoding concurrently, VERSIONINFO read-back) plus
  `avif_check_imports` / `avif_check_exports`.

## 5. Fixtures (`plugins/avif/fixtures/`)

`SOURCES.md` lists every file: origin URL + commit, licence, what it exercises, expected values.
Committed to the repo so builds are offline. Keep the total under ~10 MB.

From `https://github.com/AOMediaCodec/libavif/tree/<pinned commit>/tests/data` (BSD-2-Clause; read
`tests/data/README.md` there and copy any per-file notices; `LIBAVIF_DATA_README.md` is the
upstream notice file; `scripts/fetch-fixtures.ps1` re-downloads them against
`scripts/libavif-fixtures.sha256`):
`white_1x1.avif`, `io/kodim03_yuv420_8bpc.avif`, `io/cosmos1650_yuv444_10bpc_p3pq.avif`,
`alpha_noispe.avif`, `abc_color_irot_alpha_irot.avif`, `abc_color_irot_alpha_NOirot.avif`,
`clap_irot_imir_non_essential.avif`, `clop_irot_imor.avif`, `sofa_grid1x5_420.avif`,
`color_grid_alpha_nogrid.avif`, `colors-animated-8bpc.avif`,
`colors-animated-8bpc-alpha-exif-xmp.avif`, `colors-animated-12bpc-keyframes-0-2-3.avif`,
`colors_hdr_rec2020.avif`, `colors_sdr_srgb.avif`, `paris_icc_exif_xmp.avif`,
`draw_points_idat_progressive.avif`, `extended_pixi.avif`, `weld_sato_12B_8B_q0.avif`.

Synthetic, generated by `scripts/make-synthetic-fixtures.ps1` with the installed ffmpeg (has
`libaom-av1` and the `avif` muxer) and committed (CC0-1.0):
- `quad_rgb_lossless.avif`: 64×64, four solid quadrants (red, green, blue, white), `-pix_fmt gbrp`
  + `-aom-params lossless=1` → exact RGB round trip (identity matrix).
- `quad_yuv420.avif`: same picture, `yuv420p` limited range, lossy default → ±2 tolerance.
- `alpha_steps.avif`: `yuva444p`, lossless, three vertical bands with alpha 0 / 128 / 255 over a
  solid colour.
- `anim_3frames.avif`: three solid frames (red, green, blue), 100 ms / 200 ms / 300 ms.
- `gray_400.avif`: `gray` pixel format (monochrome 4:0:0).
- `tenbit_444.avif`: `yuv444p10le`, lossless.
- Negatives: `not_avif.png`, `not_avif.bmp`, `garbage.bin` (random 4 KiB), `truncated.avif`
  (`quad_yuv420.avif` cut at 60%).

## 6. Out of scope for v1 (document, do not implement)

HDR tone mapping, ICC colour management, gain maps, progressive preview rendering, layered/`a1lx`
selection, EXIF orientation (AVIF says `irot`/`imir` win). libavif rejects
`clap`/`irot`/`imir` properties that are not marked essential (`clap_irot_imir_non_essential.avif`);
such files are refused rather than shown untransformed.
