# AVIF.pvd — architecture

Status: v1 design, 2026-09-09. Owner: orchestrator. Implementers: Codex agents. Reviewers: Opus agents.
Rules that constrain this design live in `AGENTS.md`. Interface declarations below are canonical:
implement them as written; if something is genuinely impossible, report instead of improvising.

## 1. What the host expects (PVD interface v1, summary)

Source of truth: `third_party/pvd/PictureViewPlugin.h`. Key semantics:

- Eight `extern "C" __stdcall` exports: `pvdInit`, `pvdExit`, `pvdPluginInfo`, `pvdFileOpen`,
  `pvdPageInfo`, `pvdPageDecode`, `pvdPageFree`, `pvdFileClose`. Listed in `src/pvd/AVIF.def`.
- All strings are UTF-8. Strings we hand to the host must stay valid: plugin-info strings for the
  process lifetime, file-info strings until `pvdFileClose`.
- `pvdInit` returns `PVD_CURRENT_INTERFACE_VERSION` (1), or 0 on failure.
- `pvdFileOpen(pFileName, lFileSize, pBuf, lBuf, pImageInfo, ppContext)`:
  `pBuf`/`lBuf` is the head of the file (usually ≥ 16 KiB). If `lFileSize == 0` the file does not
  exist on disk (archive / virtual panel): `pBuf` is the **whole** file and stays valid until
  `pvdFileClose`. Otherwise open the file ourselves by name. Return `TRUE` only if we will decode it.
  Several files may be open at once in one plugin instance: all state lives in the context.
- `pvdPageInfo(ctx, iPage, pPageInfo)`: width, height, informational bpp, frame time in ms for
  animations. Page index is 0-based; out of range → `FALSE`.
- `pvdPageDecode(ctx, iPage, pDecodeInfo, callback, cbCtx)`: fill `pImage` (BGR 24 or BGRA 32,
  8 bits per channel), `nBPP`, `lImagePitch` (positive = top-down rows), `pPalette = nullptr`,
  `nColorsUsed = 0`, `Flags` (we never set `PVD_IDF_READONLY`: the buffer is ours and writable).
  `callback` may be `NULL`; if it returns `FALSE` we stop and return `FALSE`.
- `pvdPageFree(ctx, pDecodeInfo)`: release that decoded page. Host may hold several decoded pages
  of one file at once; identify the page by `pImage`.
- `pvdFileClose(ctx)`: destroy the context, including any pages not freed.
- Nothing may ever propagate out of an export: no exceptions, no crashes on hostile input.

Existing bundled decoders import only `KERNEL32.dll` and `msvcrt.dll`; ours imports `KERNEL32.dll`
only (static CRT).

## 2. Layering

```
host (0PictureView.dll)
   │ C ABI (8 exports)
   ▼
src/pvd/Exports.cpp ── composition root + 8 one-line forwards (raw pointers allowed here)
src/pvd/Shim        ── marshalling C structs ⇄ C++ values, firewall, ContextHandle
   │ IPlugin / IFileSession (C++ values only)
   ▼
src/core            ── all decisions: AvifPlugin, FileSession, Transform, Describe, PixelBuffer
   │ IFileSource / IDecoderFactory / IDecoder (C++ values only)
   ▼
src/adapters/win    ── FileMapping over CreateFileW/CreateFileMappingW/MapViewOfFile, Utf8→wide
src/adapters/avif   ── Decoder over libavif (dav1d + libyuv inside), one-to-one, no decisions
```

Dependency direction is downwards for *implementations*. Two headers are shared boundary contracts,
not layers: `src/pvd/Types.hpp` and `src/pvd/Plugin.hpp` define the values and interfaces that
`core` implements, so `core` may include exactly those two (plus `core/**`). `core` never includes
`pvd/Shim.hpp`, `pvd/ContextHandle.hpp`, `pvd/Firewall.hpp`, `pvd/PluginFactory.hpp`, `<windows.h>`
or `avif/avif.h`. `pvd` (shim side) and `adapters` never include each other; they meet only in
`Exports.cpp` / `DefaultPlugin.cpp`.

Everything is a value or a `std::unique_ptr`. Objects that are injected by reference (`IFileSource&`,
`IDecoderFactory&`) outlive their users by construction (composition root owns them in declaration
order). Classes holding references delete copy and move.

## 3. Canonical interfaces

Namespace for everything: `avifpvd`. Sub-namespaces `pvd`, `core`, `win`, `avif`.

### 3.1 `src/core/Error.hpp`

```cpp
enum class ErrorCode {
  NotAvif,            // signature check failed → host tries the next decoder
  FileOpenFailed,     // could not open/map the file by name
  ParseFailed,        // libavif could not parse the container
  DecodeFailed,       // AV1 decode failed
  ConversionFailed,   // YUV→RGB failed
  PageOutOfRange,     // page index ≥ page count
  Aborted,            // host callback asked us to stop
  TooLarge,           // exceeds DecoderOptions limits, or a byte count this process cannot address
  UnsupportedFeature, // parsed but we refuse (kept for future use, must have a test if used)
  InvalidTransform,   // clap/irot/imir data inconsistent with image size
  Internal,           // programming error surfaced as a value (e.g. buffer too small)
};
struct Error { ErrorCode code; std::string detail; };
template <class T> using Result = std::expected<T, Error>;
std::string_view name(ErrorCode);   // for diagnostics/tests
```

### 3.2 `src/pvd/Types.hpp` — host-facing values (no raw pointers)

```cpp
struct PluginInfo { std::uint32_t priority; std::string name, version, comments; };
struct ImageInfo  { std::uint32_t pageCount; bool animated; std::string formatName, compression, comments; };
struct PageInfo   { std::uint32_t width, height, bitsPerPixel, frameTimeMs; };
enum class PixelFormat { Bgr24, Bgra32 };
struct DecodedPage {                     // a view; pixel memory is owned by the session
  std::span<const std::byte> pixels;     // top-down rows
  std::uint32_t bitsPerPixel;            // 24 or 32
  std::uint32_t pitchBytes;              // width * bytesPerPixel, no padding
};
struct OpenRequest {
  std::string_view utf8FileName;
  std::uint64_t fileSize;                // 0 → `head` is the whole file and outlives the session
  std::span<const std::byte> head;
};
class Progress {                         // wraps the host callback; no callback → always continue
 public:
  using Fn = std::function<bool(std::uint32_t step, std::uint32_t steps)>;
  Progress() = default;
  explicit Progress(Fn fn);
  [[nodiscard]] bool report(std::uint32_t step, std::uint32_t steps) const;  // true = continue
 private:
  Fn fn_;
};
```

### 3.3 `src/pvd/Plugin.hpp` — the boundary interfaces

```cpp
class IFileSession {
 public:
  virtual ~IFileSession() = default;
  [[nodiscard]] virtual const ImageInfo& imageInfo() const = 0;
  [[nodiscard]] virtual core::Result<PageInfo> pageInfo(std::uint32_t page) const = 0;
  [[nodiscard]] virtual core::Result<DecodedPage> decodePage(std::uint32_t page, const Progress&) = 0;
  virtual bool freePage(std::span<const std::byte> pixels) = 0;  // false if unknown (still no-op)
};
class IPlugin {
 public:
  virtual ~IPlugin() = default;
  [[nodiscard]] virtual const PluginInfo& info() const = 0;
  [[nodiscard]] virtual core::Result<std::unique_ptr<IFileSession>> open(const OpenRequest&) = 0;
};
```

### 3.4 `src/pvd/Shim.hpp`, `ContextHandle.hpp`, `Firewall.hpp`

- `Firewall`: `template <class F> auto guarded(F&& f, decltype(f()) fallback) noexcept` — runs `f`,
  returns `fallback` on any exception. A `void` overload swallows. This is the only `catch (...)` in
  the codebase. Tested with a fake that throws `std::bad_alloc`, `std::runtime_error`, and an `int`.
- `ContextHandle`: `void* toHost(std::unique_ptr<IFileSession>)`,
  `std::unique_ptr<IFileSession> fromHost(void*)`, `IFileSession* borrow(void*)` (the pointer stays on
  the adapter line: callers immediately dereference or check null).
- `Shim` (constructed over `IPlugin&`, non-copyable):
  `UINT32 init()`, `void exit()`, `void pluginInfo(pvdInfoPlugin*)`,
  `BOOL fileOpen(const char*, INT64, const BYTE*, UINT32, pvdInfoImage*, void**)`,
  `BOOL pageInfo(void*, UINT32, pvdInfoPage*)`,
  `BOOL pageDecode(void*, UINT32, pvdInfoDecode*, pvdDecodeCallback, void*)`,
  `void pageFree(void*, pvdInfoDecode*)`, `void fileClose(void*)`.
  Every method body is `return guarded([&] { ... }, FALSE);`. Null host pointers → `FALSE`/no-op.
  `fileOpen` builds `OpenRequest`, calls `IPlugin::open`, on success fills `pvdInfoImage` from
  `ImageInfo` (strings via `c_str()` of strings owned by the session) and stores the context.
  `pageDecode` builds a `Progress` capturing the raw callback + context in a lambda, then fills
  `pvdInfoDecode` from `DecodedPage`. `pageFree` calls `freePage(span over pImage)` — the span length
  is unknown to the host, so the session matches by `data()` only.
- `Exports.cpp`: composition root. `pvdInit` creates `std::unique_ptr<IPlugin>` via
  `pvd::makePlugin()` (declared in `src/pvd/PluginFactory.hpp`, **defined** in
  `src/adapters/DefaultPlugin.cpp` so that the pvd layer can be built and unit-tested with a
  test-provided definition before the adapters exist) and a `Shim` over it (both in a
  `std::optional`/`unique_ptr` static); `pvdExit` resets. If an export is called without a live shim: `pvdPluginInfo` fills the constant
  name/version/priority with empty comments; the others return `FALSE`/no-op. Everything is a
  one-line forward wrapped in `guarded`.
- `PvdApi.hpp` includes `<Windows.h>` (lean, `NOMINMAX`) and the SDK header inside `extern "C"`
  exactly once, for `Shim`, `Exports.cpp` and the tests. `PluginConstants.hpp` holds
  `kPluginPriority = 10`, `kPluginName = "AVIF"`, `kPluginVersion = "1.0.0"`: `Shim::pluginInfo`
  writes them (with empty comments) before it consults `IPlugin::info()`, so a throwing `info()`
  leaves the host with the constant identity, and `DefaultPlugin` builds its `PluginInfo` from
  the same constants.
- Exports on x86: the eight functions are `__stdcall`, so their symbols are `_pvdInit@0`,
  `_pvdFileOpen@28`, ... while the host resolves the bare names. `AVIF.def` lists the bare names
  and is the one source of truth for both architectures: lld-link (like link.exe) resolves an
  undecorated `.def` name to the decorated `__stdcall` symbol itself, so the x86 export table
  reads `pvdInit`, `pvdFileOpen`, ... with no alias lines, no `#ifdef _M_IX86` and no
  `/EXPORT` pragmas. `scripts/check-exports.ps1` (ctest `check_exports`, Release) pins the
  export table to exactly those eight bare names on both architectures; the e2e host driver
  (`GetProcAddress` by bare name) is the behavioural acceptance test.
- Version identity: `project(VERSION)` in `CMakeLists.txt` is the single source of the version.
  `src/pvd/CMakeLists.txt` generates `pvd/Version.hpp` from `src/pvd/Version.hpp.in` (version,
  `AVIFPVD_AUTHOR` / `AVIFPVD_COPYRIGHT` cache variables, and the plugin comments built from the
  library versions vcpkg installed); `PluginConstants.hpp` `static_assert`s that `kPluginVersion`
  equals it, and `src/pvd/AVIF.rc` (llvm-rc) embeds it as the VERSIONINFO resource of `AVIF.pvd`
  (`FileVersion`/`ProductVersion`, `CompanyName`, `LegalCopyright`, `FileDescription`,
  `ProductName`/`InternalName`/`OriginalFilename` = `AVIF.pvd`, `Comments` = the `pvdPluginInfo`
  comments). The e2e test reads the block back with `GetFileVersionInfoW`/`VerQueryValueW`
  (`version.lib` is linked into `e2e_tests` only) and compares it with the running plugin.

### 3.5 `src/core/IFileSource.hpp`

```cpp
class IFileData { public: virtual ~IFileData() = default; [[nodiscard]] virtual std::span<const std::byte> bytes() const = 0; };
class IFileSource { public: virtual ~IFileSource() = default; [[nodiscard]] virtual Result<std::unique_ptr<IFileData>> open(std::string_view utf8Path) = 0; };
```

### 3.6 `src/core/IDecoder.hpp`

```cpp
enum class ChromaFormat { Yuv444, Yuv422, Yuv420, Yuv400 };
struct Cicp { std::uint16_t primaries, transfer, matrix; bool fullRange; };
struct CropRect { std::uint32_t x, y, width, height; };
enum class MirrorAxis { TopBottom, LeftRight };   // named after the effect, never a magic number
struct Transforms {
  std::optional<CropRect> clap;       // already converted from clap fractions to a pixel rect by the adapter
  std::uint8_t irotAngle = 0;         // 0..3 quarter turns, anti-clockwise (HEIF 'irot')
  std::optional<MirrorAxis> imir;
};
struct ImageMeta {
  std::uint32_t width, height;        // coded size, before transforms
  std::uint8_t depth;                 // 8, 10, 12
  ChromaFormat chroma;
  bool hasAlpha, alphaPremultiplied;
  Cicp cicp;
  std::uint32_t frameCount;           // ≥ 1
  bool animated;                      // frameCount > 1 (image sequence)
  Transforms transforms;
  bool hasIcc, hasExif, hasXmp;
};
struct FrameTiming { std::uint32_t durationMs; };
struct DecoderOptions { unsigned maxThreads; bool strict; std::uint64_t maxPixels; std::uint32_t maxDimension; };
class IDecoder {
 public:
  virtual ~IDecoder() = default;
  [[nodiscard]] virtual const ImageMeta& meta() const = 0;
  [[nodiscard]] virtual Result<FrameTiming> frameTiming(std::uint32_t frame) const = 0;
  // Decodes frame `frame` and converts it to `format`, 8 bits/channel, into `dst` with `pitchBytes`
  // per row (rows top-down, coded size). dst.size() must be ≥ pitchBytes * height, else Internal.
  [[nodiscard]] virtual Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                 std::span<std::byte> dst, std::uint32_t pitchBytes) = 0;
};
class IDecoderFactory {
 public:
  virtual ~IDecoderFactory() = default;
  [[nodiscard]] virtual bool looksLikeAvif(std::span<const std::byte> head) const = 0;
  [[nodiscard]] virtual Result<std::unique_ptr<IDecoder>> create(std::span<const std::byte> file,
                                                                 const DecoderOptions&) = 0;  // parses
};
```

### 3.7 Core classes

- `PixelBuffer` (`src/core/PixelBuffer.hpp`): owns `std::unique_ptr<std::byte[]>` made with
  `std::make_unique_for_overwrite`; fields width, height, bytesPerPixel, pitch; `bytes()` spans.
  Size arithmetic in `std::uint64_t`, checked against `DecoderOptions::maxPixels` by the caller.
- `narrow<To>` (`src/core/Narrow.hpp`): `Result<To> narrow(std::uint64_t value, std::string_view
  what)` - the one place where a 64-bit byte count becomes a `std::size_t` (`TooLarge` if `To`
  cannot hold it). `PixelBuffer::create` (pitch x height) and `win::detail::fileSize`
  (`GetFileSizeEx`) go through it, so on the 32-bit build a picture or file beyond 4 GiB is
  refused instead of wrapped; there is no `#ifdef` on the architecture anywhere in `src/`. Frame
  times use `std::llround` (`long` is 32 bits on Windows).
- `Transform` (`src/core/Transform.hpp/.cpp`): pure functions over `PixelView`
  (`std::span<const std::byte>`, width, height, bytesPerPixel, pitch):
  `Result<CropRect> validatedCrop(...)`, `std::pair<uint32,uint32> displaySize(const ImageMeta&)`,
  `bool hasTransforms(const Transforms&)`,
  `Result<PixelBuffer> apply(const Transforms&, PixelView, std::uint64_t maxPixels)` applying
  **clap → irot → imir** in that order; with no transform present `apply` returns an identity copy
  (no precondition, never throws for a well-formed view). There are no separate public
  `crop`/`rotate`/`mirror` entry points: `apply` with a single property set is the per-operation
  interface, and the tests pin each operation that way. `PixelBuffer::create(width, height,
  bytesPerPixel, maxPixels)` performs the `maxPixels`/overflow check itself and returns `TooLarge`.
  Order (AVIF spec §"Transformative properties"; cross-check the comment on `transformFlags` in the
  installed `avif/avif.h` and cite both in a code comment). `irot` angle n = n × 90° anti-clockwise.
  `imir` semantics follow the installed `avif.h` comment for `axis`; the adapter maps the number to
  `MirrorAxis`. Unit tests use 2×3 / 3×2 synthetic images with hand-derived expected outputs.
- `Describe` (`src/core/Describe.hpp/.cpp`): `std::string describe(const ImageMeta&)`. Format:
  `"<depth>-bit YUV 4:2:0 (limited range), CICP 1/13/6, straight alpha, 12 frames, ICC, EXIF, XMP, clap, irot 1, imir top-bottom"`.
  Items absent are omitted; `Yuv400` prints `YUV 4:0:0 (monochrome)`; premultiplied prints
  `premultiplied alpha`; still image prints no frames item; full range prints `full range`.
  Well-known CICP triples get a suffix: `1/13/6` → `(sRGB)`, `1/1/1` → `(BT.709)`,
  `9/16/9` → `(BT.2020 PQ)`, `9/18/9` → `(BT.2020 HLG)`, `12/16/12` → `(P3 PQ)`; `2/2/2` → `(unspecified)`.
- `AvifPlugin : pvd::IPlugin` (`src/core/AvifPlugin.hpp/.cpp`), ctor `(IFileSource&, IDecoderFactory&, DecoderOptions, PluginInfo)`.
  `open()`: `looksLikeAvif(head)` else `NotAvif`; if `fileSize == 0` data = `head`, else
  `fileSource.open(name)` → `IFileData` owned by the session; `factory.create(data, options)`;
  build `ImageInfo{frameCount, animated, "AVIF", "AV1", describe(meta)}`; return `FileSession`.
- `FileSession : pvd::IFileSession` (`src/core/FileSession.hpp/.cpp`): owns
  `std::unique_ptr<IFileData>` (null in memory mode), `std::unique_ptr<IDecoder>`, `ImageInfo`,
  `std::vector<std::unique_ptr<PixelBuffer>> outstanding_`, `DecoderOptions`.
  - `pageInfo(p)`: range check; `displaySize(meta)`; `bitsPerPixel = depth × (hasAlpha ? 4 : 3)`
    (informational, so 10-bit RGBA reports 40); `frameTimeMs` = `frameTiming(p)` when animated else 0.
  - `decodePage(p, progress)`: range check; `progress.report(0, 3)`; allocate `PixelBuffer` at coded
    size (`Bgra32` if `hasAlpha` else `Bgr24`); `decodeFrame`; `progress.report(1, 3)`;
    if any transform present → `Transform::apply` into a new buffer; `progress.report(2, 3)`;
    move buffer into `outstanding_`; return the view. Any `false` from `report` → `Aborted`.
  - `freePage(span)`: erase the buffer whose `bytes().data() == span.data()`; return whether found.
- `DefaultPlugin` (`src/adapters/DefaultPlugin.cpp`): defines `pvd::makePlugin()`
  returning an object that owns `win::FileSource`, `avif::DecoderFactory`, and `core::AvifPlugin`
  (declared in that order) and forwards `IPlugin`. Options: `maxThreads = max(1, hardware_concurrency())`,
  `strict = false`, `maxPixels = 16384 × 16384`, `maxDimension = 32768`.
  `PluginInfo{10, "AVIF", "1.0.0", "AVIF decoder: libavif <ver>, dav1d <ver>, libyuv <ver>; static build"}`
  (versions from `avifVersion()`, `dav1d_version()`, `LIBYUV_VERSION`).

### 3.8 Adapters

- `win::FileMapping` (`src/adapters/win/FileMapping.hpp/.cpp`): RAII over `CreateFileW` (read,
  share read/write/delete, `FILE_FLAG_SEQUENTIAL_SCAN`), `CreateFileMappingW(PAGE_READONLY)`,
  `MapViewOfFile(FILE_MAP_READ)`; each handle in a `std::unique_ptr` with a stateless deleter;
  implements `IFileData`. Zero-length file → `FileOpenFailed` (mapping of 0 bytes is illegal).
  `win::FileSource : IFileSource` converts UTF-8 → UTF-16 with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)`
  and prefixes `\\?\` **only for long paths**: paths shorter than `MAX_PATH` are passed to
  `CreateFileW` unchanged (Win32 normalisation of `/`, `.`, `..` stays active); at or above
  `MAX_PATH` the path is first normalised with `GetFullPathNameW` and then prefixed (`\\?\X:\...`,
  UNC → `\\?\UNC\server\share\...`); paths already starting with `\\?\` or `\\.\` (either slash
  spelling) are never touched — a `\\.\` file path ≥ `MAX_PATH` will not open, that is the caller's
  choice; and if `GetFullPathNameW` returns a path that already carries one of these prefixes it is
  used as is.
  The `imageSizeLimit` handed to libavif is clamped to `[1, AVIF_DEFAULT_IMAGE_SIZE_LIMIT]` (libavif
  rejects anything else); the real `maxPixels` limit is enforced by `PixelBuffer::create` in core.
  The `file` span given to `DecoderFactory::create` must outlive the decoder (libavif keeps pointers
  into it); `FileSession` guarantees this by declaring the file data before the decoder.
- `avif::Decoder` (`src/adapters/avif/Decoder.hpp/.cpp`): `std::unique_ptr<avifDecoder, Destroy>`;
  `create()`: `avifDecoderCreate`, set `maxThreads`, `strictFlags = strict ? AVIF_STRICT_ENABLED : AVIF_STRICT_DISABLED`,
  `imageSizeLimit`, `imageDimensionLimit`, `avifDecoderSetIOMemory`, `avifDecoderParse`, then fill
  `ImageMeta` from `decoder->image` (width/height/depth/yuvFormat/yuvRange/colorPrimaries/
  transferCharacteristics/matrixCoefficients, `alphaPresent`, `alphaPremultiplied`, `imageCount`,
  `transformFlags` + `clap` (via `avifCropRectFromCleanApertureBox`; invalid → `InvalidTransform`),
  `irot.angle`, `imir.axis`, `icc.size`, `exif.size`, `xmp.size`). Maps `avifResult` to `ErrorCode`
  with `avifResultToString` in `detail`.
  `frameTiming`: `avifDecoderNthImageTiming` → `lround(duration × 1000)` clamped to `uint32`.
  `decodeFrame`: `avifDecoderNthImage`; `avifRGBImageSetDefaults`; `depth = 8`;
  `format = BGRA or BGR`; `alphaPremultiplied = AVIF_FALSE` (straight alpha out);
  `chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC`; `pixels = dst.data()`, `rowBytes = pitch`;
  `avifImageYUVToRGB`. No allocation inside the adapter.
  `DecoderFactory::looksLikeAvif` = `avifPeekCompatibleFileType` (returns false for short spans).
- Grid images, progressive files, 10/12-bit sources, image sequences: handled by libavif; we only
  pass through. HDR (PQ/HLG) is **not** tone-mapped in v1: colours are converted by matrix only.
  ICC profiles are ignored (the PVD interface has no colour management), as in every bundled decoder.

## 4. Build

- CMake ≥ 3.28, presets in `CMakePresets.json`: `debug`, `release`, `coverage` (x64) and
  `debug-x86`, `release-x86`, `coverage-x86` (32-bit; all Ninja, clang-cl, lld-link). The x86
  presets differ from the x64 ones only in `VCPKG_TARGET_TRIPLET = x86-windows-static-clang`;
  `cmake/vcpkg-root.cmake` reads `triplets/<triplet>.cmake` to pick the chainload toolchain
  (`cmake/clang-cl-x86.toolchain.cmake`: the same x64-hosted clang-cl cross-compiling with
  `CMAKE_<LANG>_COMPILER_TARGET = i686-pc-windows-msvc`), so the triplet file is the one place
  that maps an architecture to its toolchain. The x86 triplet also sets `VCPKG_LOAD_VCVARS_ENV ON`:
  vcpkg does not load vcvars for chainloaded triplets, and without it meson (dav1d) activates
  `vcvars64.bat` itself and links i686 objects against the x64 CRT (`_mainCRTStartup` unresolved).
  The plugin is `build/release-x86<suffix>/src/pvd/AVIF.pvd` (`IMAGE_FILE_MACHINE_I386`).
  `release`: `/O2 /GL-`? no LTO needed; `/MT`, `/DEBUG:NONE`. `coverage`: `/Od /Zi /MT`
  + `-fprofile-instr-generate -fcoverage-mapping` on `src/**` and tests.
- Common flags: `/clang:-std=c++23 /W4 /WX /permissive- /utf-8 /EHsc /Zc:preprocessor`
  plus `-Wno-` nothing unless justified. `static_assert(__cplusplus >= 202302L)` in `src/core/Error.hpp`.
  The `coverage-x86` build links `clang_rt.profile-i386.lib` from the x86-hosted LLVM of the same
  Build Tools explicitly (the x64-hosted one ships only the x86_64 runtime, so clang names an
  arch-less `clang_rt.profile.lib` that exists nowhere; `/NODEFAULTLIB` drops that name).
- Resources: `project(... LANGUAGES CXX RC)`; `src/pvd/AVIF.rc` is compiled by llvm-rc (from the
  chainload toolchain) and linked into `AVIF.pvd`; it adds no imports (`check_imports`).
- vcpkg manifest `vcpkg.json`: `libavif[dav1d]`, `doctest`. Custom triplet
  `triplets/x64-windows-static-clang.cmake` = `x64-windows-static` + chainload
  `cmake/clang-cl.toolchain.cmake` so libyuv gets its SIMD paths (vcpkg#28446) and everything is one
  toolchain. Set `VCPKG_OVERLAY_TRIPLETS` in the presets. If dav1d's meson build refuses clang-cl,
  report; do not silently fall back.
  The chainload toolchain sets only: compilers, linker, resource compiler, and
  `CMAKE_MSVC_RUNTIME_LIBRARY`. It must **not** replace `CMAKE_AR` or override CMake's archive rules:
  libavif 1.4.2's `merge_static_libs.cmake` mis-detects clang-cl (it tests the Clang compiler id
  before `MSVC`), and the fix for that is an **overlay port** `ports/libavif` (copy of the vcpkg port
  plus one patch that checks `MSVC` first so the merge uses the lib.exe-style bundling, with
  `CMAKE_LIBTOOL`/llvm-lib), registered through `VCPKG_OVERLAY_PORTS` in the presets.
- Targets: `avifpvd_core` (static), `avifpvd_pvd` (static, Shim/ContextHandle/Firewall),
  `avifpvd_adapters` (static, links `avif`, `dav1d`, `yuv`), `avifpvd` (SHARED; `OUTPUT_NAME AVIF`,
  `SUFFIX .pvd`, `PREFIX ""`, `/DEF:src/pvd/AVIF.def`), tests: `pvd_tests`, `core_tests`,
  `adapter_tests`, `e2e_tests`, `guard_tests`. Per-directory `CMakeLists.txt` with
  `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` so parallel agents do not fight over one file.
- `scripts/check-imports.ps1`: fails unless the import table of `AVIF.pvd` is exactly `KERNEL32.dll`
  (uses `dumpbin /dependents` from Build Tools, or `llvm-readobj --coff-imports`). Registered as a
  ctest test in the release presets (`check_imports`). `scripts/check-exports.ps1` (`check_exports`)
  does the same for the export table: exactly the eight bare `pvd*` names.
- Build directories: `build/<preset>$env{AVIFPVD_BUILD_SUFFIX}` so several agents can build the
  same tree concurrently without sharing a Ninja/CMake state directory.
- `scripts/coverage.ps1 [-Preset coverage|coverage-x86]`: configures+builds the preset, runs the
  five test executables under `LLVM_PROFILE_FILE`, merges with `llvm-profdata`, runs `llvm-cov report` with
  `-ignore-filename-regex` excluding `tests/`, `third_party/`, `vcpkg`, prints the totals, writes HTML
  to `build/coverage/html`, exits 1 unless lines == 100% and branches == 100% for `src/**`.
  The same 100 % gate applies to both presets.
- `scripts/build-all.ps1`: builds `release` and `release-x86`, runs both gates on each DLL and
  copies them to `dist/x64/AVIF.pvd` and `dist/x86/AVIF.pvd`. `scripts/package.ps1`: the same from
  scratch (suffix `-pkg`), then `dist/AVIF-<version>-x64.zip` and `-x86.zip` (`AVIF.pvd`,
  `README.txt`, `LICENSES.txt` from vcpkg's `share/<port>/copyright` files) with their SHA-256.

## 5. Tests

- Framework: doctest (vcpkg). One executable per directory: `pvd_tests`, `core_tests`,
  `adapter_tests`, `e2e_tests`, `guard_tests`. Parallel agents own disjoint directories.
- `tests/pvd`: `Firewall`, `ContextHandle`, `Shim` (fake `IPlugin`/`IFileSession`: success, every
  `ErrorCode`, throwing, null host pointers, callback abort), `Progress`.
- `tests/core`: `PixelBuffer`, `Transform`
  (all angles, both axes, crop, combined, invalid crop), `Describe` (every branch), `AvifPlugin`
  and `FileSession` on fake `IFileSource`/`IDecoderFactory`/`IDecoder` (memory mode vs file mode,
  open failure, parse failure, page range, decode failure, conversion failure, abort at each step,
  transforms present/absent, alpha/no alpha, animated timing, freePage known/unknown, size limit).
- `tests/adapters`: `FileMapping`/`FileSource` on temp files (normal, empty, missing, non-ASCII
  name, long path); `avif::Decoder` on fixtures (meta for each fixture, timing for animations,
  decode into caller buffer, buffer too small, truncated file, non-AVIF, strict vs lenient).
- `tests/e2e`: `LoadLibraryW(AVIF.pvd)`, `GetProcAddress` all 8, drive them like the host: init →
  pluginInfo (priority 10, name "AVIF", version "1.0.0") → fileOpen from disk and from memory
  (`lFileSize = 0`) → pageInfo → pageDecode with and without callback, callback abort → pageFree →
  fileClose → exit. Pixel assertions on synthetic fixtures (exact for lossless RGB, ±2 for YUV),
  page count / frame times on animations, dimensions after irot on rotated fixtures, rejection of
  PNG/BMP/garbage/truncated input, no crash on all libavif fixtures listed below.
- `tests/guard`: walks `src/` and fails on forbidden tokens: `new `, `new(`, `delete `, `malloc`,
  `calloc`, `realloc`, `free(`, `shared_ptr`, `weak_ptr`, `reinterpret_cast` outside adapters/pvd,
  `#include <windows.h>` / `avif/avif.h` outside `src/adapters/**` and `src/pvd/Exports.cpp`,
  `catch (` outside `Firewall.hpp`, `LCOV_EXCL`, `__builtin_unreachable`, `[[assume`.
  Comments and string literals are stripped before matching so the rule text itself does not trip it.

## 6. Fixtures (`tests/fixtures/`)

`SOURCES.md` lists every file: origin URL + commit, licence, what it exercises, expected values.
Committed to the repo so builds are offline. Keep the total under ~10 MB.

From `https://github.com/AOMediaCodec/libavif/tree/<pinned commit>/tests/data` (BSD-2-Clause; read
`tests/data/README.md` there and copy any per-file notices):
`white_1x1.avif`, `io/kodim03_yuv420_8bpc.avif`, `io/cosmos1650_yuv444_10bpc_p3pq.avif`,
`alpha_noispe.avif`, `abc_color_irot_alpha_irot.avif`, `abc_color_irot_alpha_NOirot.avif`,
`clap_irot_imir_non_essential.avif`, `clop_irot_imor.avif`, `sofa_grid1x5_420.avif`,
`color_grid_alpha_nogrid.avif`, `colors-animated-8bpc.avif`,
`colors-animated-8bpc-alpha-exif-xmp.avif`, `colors-animated-12bpc-keyframes-0-2-3.avif`,
`colors_hdr_rec2020.avif`, `colors_sdr_srgb.avif`, `paris_icc_exif_xmp.avif`,
`draw_points_idat_progressive.avif`, `extended_pixi.avif`, `weld_sato_12B_8B_q0.avif`.

Synthetic, generated by `scripts/make-synthetic-fixtures.ps1` with the installed ffmpeg (has
`libaom-av1` and the `avif` muxer) and committed:
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

## 7. Concurrency and lifetime

- The host may decode several files at once (prefetch). Every session is independent; the only
  process-wide state is the composition root created in `pvdInit`. libavif/dav1d are thread-safe
  across decoder instances.
- A `DecodedPage` view is valid until `freePage` or session destruction, whichever comes first.
- In memory mode the file bytes belong to the host and are valid until `pvdFileClose` (SDK
  guarantee); the session stores only a `std::span` and no `IFileData`.
- Known limitation: the file is mapped with `FILE_SHARE_WRITE | FILE_SHARE_DELETE` (so Far can keep
  working with the file while it is shown). If another process truncates the file while a page is
  being decoded, reading the mapped view raises `EXCEPTION_IN_PAGE_ERROR`, a structured exception
  that the C++ `catch (...)` firewall does not see. This is the same behaviour as the bundled
  decoders that map files (e.g. BMP.pvd) and is accepted for v1.

## 8. Out of scope for v1 (document, do not implement)

HDR tone mapping, ICC colour management, gain maps, progressive preview rendering, layered/`a1lx`
selection, EXIF orientation (AVIF says `irot`/`imir` win), 16-bit output.
