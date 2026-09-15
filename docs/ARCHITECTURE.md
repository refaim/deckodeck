# deckodeck — architecture

Status: v2 design, 2026-09-13 (v1 was the single-plugin AVIF.pvd design of 2026-09-09; Task 7
turned it into this monorepo). Owner: orchestrator. Implementers: Codex/Claude agents. Reviewers:
Opus agents. Rules that constrain this design live in `AGENTS.md`. Interface declarations below
are canonical: implement them as written; if something is genuinely impossible, report instead
of improvising. Plugin-specific design (what AVIF.pvd does with libavif, its fixtures, its
limitations) lives next to the plugin: `plugins/avif/DESIGN.md`. "deckodeck" names the product and
the repository (`https://github.com/refaim/deckodeck`); `pvdkit`, used throughout this document,
names the shared PVD kit layer underneath both plugins (`docs/BUILD.md` explains the split).

## 0. Shape of the repository

```
pvdkit/
  src/pvd/            shared: the PVD boundary (Types, Plugin, Progress, Shim, ContextHandle,
                      Firewall, PvdApi, PluginFactory, PluginIdentity, Exports.cpp, Plugin.def,
                      Plugin.rc, PluginConstants.hpp.in)                      → pvdkit_pvd
  src/core/           shared: Error, Narrow, PixelBuffer, Transform, IDecoder, IFileSource,
                      IImageDescriber, CodecPlugin, FileSession                → pvdkit_core
  src/adapters/win/   shared: FileMapping, FileSource, Utf8                    → pvdkit_win
  plugins/<id>/       one directory per plugin (today: avif → AVIF.pvd, rpgmvp → RPGMVP.pvd)
    CMakeLists.txt    identity (name, version, priority), libraries, composition, packaging
    src/core/         plugin decisions without the codec library (e.g. the describer)  → <id>_core
    src/adapters/     the codec adapter(s)                                     → <id>_adapter
    src/DefaultPlugin.cpp   the composition root: pvd::makePlugin()            → <id>_composition
    tests/core/ tests/adapters/ tests/e2e/   the plugin's tests
    fixtures/  scripts/  package/{readme_en.txt,readme_ru.txt,ChangeLog}  README.md  DESIGN.md
  tests/pvd/ tests/core/ tests/adapters/ tests/guard/   shared tests
  tests/support/      the leak gate: LeakCheck (accounting), HostileCorpus (fuzz-lite corpus),
                      leak/LeakScenarios.cpp compiled into every plugin's <id>_leak_tests
  tests/e2e/          the shared host driver + VERSIONINFO test, compiled into every plugin's e2e
  cmake/pvdkit-plugin.cmake   pvdkit_plugin_identity / pvdkit_add_plugin / pvdkit_add_plugin_e2e_tests
  scripts/            coverage, check-imports, check-exports, lint, build-all, pack (zips from
                      existing release builds), package (build-all + lint on both architectures +
                      the asan preset + pack), the release helpers release-tag / release-notes /
                      update-readme-downloads (.github/workflows/release.yml), llvm-dir (shared)
  .clang-format .clang-tidy tests/.clang-tidy cppcheck-suppressions.txt PSScriptAnalyzerSettings.psd1
  binskim.psd1        the analyzer configuration scripts/lint.ps1 reads (Task 9)
  ports/ triplets/ vcpkg.json CMakePresets.json   one toolchain and one manifest for the kit
  docs/               this file, BUILD.md (build, packaging and CI) and the task history
```

Every plugin is one DLL `<NAME>.pvd` = the shared `Exports.cpp` + `Plugin.def` + `Plugin.rc`
compiled against the plugin's generated identity, linked with `pvdkit_pvd` and the plugin's
composition root. Every plugin ships x64 and x86 with the same guarantees: static CRT,
`KERNEL32.dll` as the only import, the eight bare exports, 100 % line and branch coverage of
`src/**` and `plugins/*/src/**`, a VERSIONINFO resource, a zip per architecture.

## 1. What the host expects (PVD interface v1, summary)

Source of truth: `third_party/pvd/PictureViewPlugin.h`. Key semantics:

- Eight `extern "C" __stdcall` exports: `pvdInit`, `pvdExit`, `pvdPluginInfo`, `pvdFileOpen`,
  `pvdPageInfo`, `pvdPageDecode`, `pvdPageFree`, `pvdFileClose`. Listed in `src/pvd/Plugin.def`,
  identical for every plugin.
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
- `pvdPageDecode(ctx, iPage, pDecodeInfo, callback, cbCtx)`: fill `pImage` (BGR 24, BGRA 32, or
  BGRA 64 with little-endian 16-bit samples), `nBPP`, `lImagePitch` (positive = top-down rows),
  `pPalette = nullptr`, `nColorsUsed = 0`, `Flags` (`PVD_IDF_ALPHA`, the host's undocumented bit 2,
  when alpha is meaningful; never `PVD_IDF_READONLY`, because the buffer is ours and writable).
  `callback` may be `NULL`; if it returns `FALSE` we stop and return `FALSE`.
  PictureView testing on 2026-09-13 established that `nBPP == 64` is accepted and rendered like
  the 8-bit path; its 10-bit output, spatial dithering, gamma-correct scaling and auto-levels can
  use the preserved precision.
- `pvdPageFree(ctx, pDecodeInfo)`: release that decoded page. Host may hold several decoded pages
  of one file at once; identify the page by `pImage`.
- `pvdFileClose(ctx)`: destroy the context, including any pages not freed.
- Nothing may ever propagate out of an export: no exceptions, no crashes on hostile input.

Existing bundled decoders import only `KERNEL32.dll` and `msvcrt.dll`; ours import `KERNEL32.dll`
only (static CRT).

## 2. Layering

```
host (0PictureView.dll)
   │ C ABI (8 exports)
   ▼
src/pvd/Exports.cpp ── composition root + 8 one-line forwards (raw pointers allowed here);
                       compiled once per plugin against that plugin's pvd/PluginConstants.hpp
src/pvd/Shim        ── marshalling C structs ⇄ C++ values, firewall, ContextHandle
   │ IPlugin / IFileSession (C++ values only)
   ▼
src/core            ── all format-neutral decisions: CodecPlugin, FileSession, Transform, PixelBuffer
   │ IFileSource / IDecoderFactory / IDecoder / IImageDescriber (C++ values only)
   ▼
src/adapters/win    ── FileMapping over CreateFileW/CreateFileMappingW/MapViewOfFile, Utf8→wide
plugins/<id>/src/core      ── the plugin's decisions without its library (IImageDescriber, ...)
plugins/<id>/src/adapters  ── the codec adapter (IDecoderFactory/IDecoder), one-to-one, no decisions
plugins/<id>/src/DefaultPlugin.cpp ── pvd::makePlugin(): owns FileSource, the factory, the
                                      describer and a CodecPlugin, in that order
```

Dependency direction is downwards for *implementations*. Two headers are shared boundary contracts,
not layers: `src/pvd/Types.hpp` and `src/pvd/Plugin.hpp` define the values and interfaces that
`core` implements, so `core` (shared or a plugin's) may include exactly those two (plus `core/**`).
`core` never includes `pvd/Shim.hpp`, `pvd/ContextHandle.hpp`, `pvd/Firewall.hpp`,
`pvd/PluginFactory.hpp`, `pvd/PluginConstants.hpp`, `<windows.h>` or a codec header. `pvd` (shim
side) and `adapters` never include each other; they meet only in `Exports.cpp` /
`DefaultPlugin.cpp`.

Include ownership across roots: shared `src/**` never includes anything under `plugins/**`; a
plugin includes shared headers and its own, never another plugin's. Each plugin has its own
include root (`plugins/<id>/src`), so plugin code says `#include "adapters/avif/Decoder.hpp"` and
`#include "core/Describe.hpp"` exactly like shared code says `#include "core/Error.hpp"`; the
build enforces the ownership (the shared libraries do not see the plugin roots) and the guard test
enforces it independently by resolving every include against the source roots (§5).

Everything is a value or a `std::unique_ptr`. Objects that are injected by reference (`IFileSource&`,
`IDecoderFactory&`, `const IImageDescriber&`, `const colour::SrgbOutputTables&`) outlive their users
by construction (the composition root owns them in declaration order). Classes holding references
delete copy and move. There is no other way to hold state across sessions: no function-local
static, no namespace-scope object with a constructor or destructor (§7).

## 3. Canonical interfaces

Namespace for everything shared: `pvdkit`, with sub-namespaces `pvd`, `core`, `core::colour`,
`win`. A plugin
uses a sub-namespace named after itself for all of its code (`pvdkit::avif`), including the
describer it places under `src/core/`.

### 3.1 `src/core/Error.hpp`

```cpp
enum class ErrorCode : std::uint8_t {   // every enum names its base type (clang-tidy performance-enum-size)
  NotRecognised,      // signature check failed → host tries the next decoder
  FileOpenFailed,     // could not open/map the file by name
  ParseFailed,        // the container could not be parsed
  DecodeFailed,       // bitstream decode failed
  ConversionFailed,   // colour conversion to BGR/BGRA failed
  PageOutOfRange,     // page index ≥ page count
  Aborted,            // host callback asked us to stop
  TooLarge,           // exceeds DecoderOptions limits, or a byte count this process cannot address
  UnsupportedFeature, // parsed but we refuse (kept for future use, must have a test if used)
  InvalidTransform,   // clap/irot/imir data inconsistent with image size
  Internal,           // programming error surfaced as a value (e.g. buffer too small)
};
struct Error { ErrorCode code = ErrorCode::Internal; std::string detail; };   // scalar members carry defaults (cppcheck)
template <class T> using Result = std::expected<T, Error>;
std::string_view name(ErrorCode);   // for diagnostics/tests
```

### 3.2 `src/pvd/Types.hpp` — host-facing values (no raw pointers)

```cpp
struct PluginInfo { std::uint32_t priority = 0; std::string name, version, comments; };
struct ImageInfo  { std::uint32_t pageCount = 0; bool animated = false; std::string formatName, compression, comments; };
struct PageInfo   { std::uint32_t width, height, bitsPerPixel, frameTimeMs; };
enum class PixelFormat : std::uint8_t { Bgr24, Bgra32, Bgra64 }; // Bgra64 = LE 16-bit samples, straight alpha
struct DecodedPage {                     // a view; pixel memory is owned by the session
  std::span<const std::byte> pixels;     // top-down rows
  std::uint32_t bitsPerPixel = 0;        // 24, 32 or 64
  std::uint32_t pitchBytes = 0;          // width * bytesPerPixel, no padding
  bool hasAlpha = false;                 // alpha carries information, rather than being fully opaque
  std::uint8_t hostOrientation = 0;      // 0..7, PictureView's orientation code
};
struct OpenRequest {
  std::string_view utf8FileName;
  std::uint64_t fileSize = 0;            // 0 → `head` is the whole file and outlives the session
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

### 3.4 `src/pvd/Shim.hpp`, `ContextHandle.hpp`, `Firewall.hpp`, `PluginIdentity.hpp`

- `Firewall`: `template <class F> auto guarded(F&& f, decltype(f()) fallback) noexcept` — runs `f`,
  returns `fallback` on any exception. A `void` overload delegates to it (wrapping `f` in a lambda
  that returns `true`) and swallows, so the value overload holds the only `catch (...)` in the
  codebase. Tested with a fake that throws `std::bad_alloc`, `std::runtime_error`, and an `int`.
- `ContextHandle`: `void* toHost(std::unique_ptr<IFileSession>)`,
  `std::unique_ptr<IFileSession> fromHost(void*)`, `IFileSession* borrow(void*)` (the pointer stays on
  the adapter line: callers immediately dereference or check null).
- `PluginIdentity` (`src/pvd/PluginIdentity.hpp`): `struct { std::uint32_t priority = 0;
  std::string_view name, version; }` — the constant identity of one plugin, literal-backed (the
  host receives `data()` as C strings). The shared layer only forwards a value it is given; each
  plugin's value `kPluginIdentity` comes from its generated `pvd/PluginConstants.hpp` (§3.4a).
- `Shim` (constructed over `IPlugin&` and a `const PluginIdentity&`, copied into the member,
  non-copyable):
  `UINT32 init()`, `void exit()`, `void pluginInfo(pvdInfoPlugin*)`,
  `BOOL fileOpen(const char*, INT64, const BYTE*, UINT32, pvdInfoImage*, void**)`,
  `BOOL pageInfo(void*, UINT32, pvdInfoPage*)`,
  `BOOL pageDecode(void*, UINT32, pvdInfoDecode*, pvdDecodeCallback, void*)`,
  `void pageFree(void*, pvdInfoDecode*)`, `void fileClose(void*)`.
  Every method body is `return guarded([&] { ... }, FALSE);`. Null host pointers → `FALSE`/no-op.
  `pluginInfo` first writes the identity (`fillDefaultPluginInfo(output, identity)`: priority,
  name, version, empty comments) and only then consults `IPlugin::info()`, so a throwing `info()`
  leaves the host with the constant identity.
  `fileOpen` builds `OpenRequest`, calls `IPlugin::open`, on success fills `pvdInfoImage` from
  `ImageInfo` (strings via `c_str()` of strings owned by the session) and stores the context.
  `pageDecode` builds a `Progress` capturing the raw callback + context in a lambda, then fills
  `pvdInfoDecode` from `DecodedPage`, including undocumented `PVD_IDF_ALPHA` bit 2 when `hasAlpha`
  is true and `DecodedPage::hostOrientation << PVD_IDF_ORIENTATION_SHIFT` in the high nibble.
  `PvdApi.hpp` owns both declarations and records their provenance; the recovered host behaviour and
  EXIF-to-code table live in [`docs/host/pictureview-abi.md`](host/pictureview-abi.md). The shim writes
  only the documented `pvdInfoDecode` fields.
  `pageFree` calls `freePage(span over pImage)` — the span length
  is unknown to the host, so the session matches by `data()` only.
- `Exports.cpp`: the shared composition root, compiled once into every plugin DLL (and into
  `pvd_tests`). `pvdInit` creates `std::unique_ptr<IPlugin>` via `pvd::makePlugin()` (declared in
  `src/pvd/PluginFactory.hpp`, **defined** in the plugin's `DefaultPlugin.cpp`, or by the test) and
  a `Shim` over it and `kPluginIdentity`; `pvdExit` resets. If an export is called without a live
  shim: `pvdPluginInfo` fills the identity with empty comments; the others return `FALSE`/no-op.
  Everything is a one-line forward wrapped in `guarded`. It is the only shared file that may
  include the generated `pvd/PluginConstants.hpp` (guard rule).
- `PvdApi.hpp` includes `<Windows.h>` (lean, `NOMINMAX`) and the SDK header inside `extern "C"`
  exactly once, for `Shim`, `Exports.cpp` and the tests. It also declares the two observed decode
  flag conventions used by the shim: alpha bit 2 and the PictureView orientation code beginning at
  bit 4. Their binary provenance is recorded in [`docs/host/pictureview-abi.md`](host/pictureview-abi.md).
- Exports on x86: the eight functions are `__stdcall`, so their symbols are `_pvdInit@0`,
  `_pvdFileOpen@28`, ... while the host resolves the bare names. `Plugin.def` lists the bare names
  and is the one source of truth for both architectures: lld-link (like link.exe) resolves an
  undecorated `.def` name to the decorated `__stdcall` symbol itself, so the x86 export table
  reads `pvdInit`, `pvdFileOpen`, ... with no alias lines, no `#ifdef _M_IX86` and no
  `/EXPORT` pragmas. `scripts/check-exports.ps1` (ctest `<id>_check_exports`, Release) pins the
  export table to exactly those eight bare names on both architectures; the e2e host driver
  (`GetProcAddress` by bare name) is the behavioural acceptance test.

### 3.4a Plugin identity: one source of truth per plugin

Decision (Task 7, open point 2). A plugin's name, version and priority are declared exactly once,
in its `CMakeLists.txt`:

```cmake
pvdkit_plugin_identity(avif NAME AVIF VERSION <version> PRIORITY 10
                       DESCRIPTION "AVIF decoder plugin for PictureView (Far Manager)"
                       COMMENTS "AVIF decoder: libavif <v>, dav1d <v>, libyuv <v>; static build")
```

`pvdkit_plugin_identity` (`cmake/pvdkit-plugin.cmake`) configures `src/pvd/PluginConstants.hpp.in`
into `<build>/generated/<id>/pvd/PluginConstants.hpp` and publishes that directory through the
INTERFACE target `<id>_identity`. The generated header carries the macros
`PVDKIT_PLUGIN_NAME`, `_FILENAME` (`<NAME>.pvd`), `_PRIORITY`, `_VERSION_MAJOR/MINOR/PATCH`,
`_VERSION`, `_AUTHOR`, `_COPYRIGHT` (from the `PVDKIT_AUTHOR` / `PVDKIT_COPYRIGHT` cache
variables), `_DESCRIPTION`, `_COMMENTS`, and — outside `RC_INVOKED` — the C++ constant
`inline constexpr PluginIdentity kPluginIdentity{priority, name, version}` with `static_assert`s
that the views are literal-backed (null-terminated). Consumers:

- `src/pvd/Exports.cpp` (the `Shim`'s identity and the pre-`pvdInit` `pvdPluginInfo` answer);
- `plugins/<id>/src/DefaultPlugin.cpp` (`PluginInfo{kPluginIdentity.priority, name, version, comments}`
  — the comments are built at run time from the linked libraries, and the e2e version test pins
  them equal to the resource's `PVDKIT_PLUGIN_COMMENTS`);
- `src/pvd/Plugin.rc`, the one VERSIONINFO resource for every plugin (`FileVersion` /
  `ProductVersion`, `CompanyName`, `LegalCopyright`, `FileDescription`, `ProductName` /
  `InternalName` / `OriginalFilename` = `<NAME>.pvd`, `Comments`), compiled by llvm-rc once per
  plugin target;
- `tests/e2e/VersionResourceTests.cpp` (reads the block back with `GetFileVersionInfoW` /
  `VerQueryValueW`, `version.lib` linked into the e2e test only, and compares it with the running
  plugin's `pvdPluginInfo`);
- the package manifest `<build>/plugins/<id>/package/manifest.json` that `pvdkit_add_plugin`
  writes for `scripts/package.ps1`; the plugin's static distribution documents are copied
  byte-for-byte after their identity, encoding and line endings are validated.

The shared static library `pvdkit_pvd` carries no identity at all. `pvd_tests` compiles
`Exports.cpp` against its own generated identity (`TestPlugin` 9.8.7, priority 42) through the
same function, which is how the test proves the plumbing rather than a constant. There is no
project-wide version: the kit has none, each plugin has its own.

### 3.5 `src/core/IFileSource.hpp`

```cpp
class IFileData { public: virtual ~IFileData() = default; [[nodiscard]] virtual std::span<const std::byte> bytes() const = 0; };
class IFileSource { public: virtual ~IFileSource() = default; [[nodiscard]] virtual Result<std::unique_ptr<IFileData>> open(std::string_view utf8Path) = 0; };
```

### 3.6 `src/core/IDecoder.hpp`, `src/core/IImageDescriber.hpp`

```cpp
enum class ChromaFormat : std::uint8_t { Yuv444, Yuv422, Yuv420, Yuv400 };
struct Cicp { std::uint16_t primaries, transfer, matrix; bool fullRange; };
struct CropRect { std::uint32_t x, y, width, height; };
enum class MirrorAxis : std::uint8_t { TopBottom, LeftRight };   // named after the effect, never a magic number
struct Transforms {                               // every field at its default = "none"
  std::optional<CropRect> clap;       // already converted from clap fractions to a pixel rect by the adapter
  std::uint8_t irotAngle = 0;         // 0..3 quarter turns, anti-clockwise (HEIF 'irot')
  std::optional<MirrorAxis> imir;
};
struct ImageMeta {                    // every scalar has a default: an empty ImageMeta{} is a valid value
  std::uint32_t width = 0, height = 0; // coded size, before transforms
  std::uint8_t depth = 0;             // bits per sample of the source (8, 10, 12, ...)
  ChromaFormat chroma = ChromaFormat::Yuv444;   // sample layout in CICP terms (see below)
  bool hasAlpha = false, alphaPremultiplied = false;
  Cicp cicp{};
  std::uint32_t frameCount = 0;       // ≥ 1 once parsed
  bool animated = false;              // frameCount > 1 (image sequence)
  Transforms transforms;
  bool hasIcc = false, hasExif = false, hasXmp = false;
  bool indexed = false;                 // source stores palette indices; depth is the index width (1/2/4/8)
  bool interlaced = false;              // source is stored progressively (PNG Adam7; informational only)
  std::optional<float> masteringPeakNits; // HDR mastering/content peak in cd/m2, when supplied
  std::uint8_t exifOrientation = 0;      // 0 = absent/ignored; 1..8 are EXIF orientation values
};
struct FrameTiming { std::uint32_t durationMs; };
struct DecoderOptions {
  unsigned maxThreads = 0;
  bool strict = false;
  std::uint64_t maxPixels = 0;
  std::uint32_t maxDimension = 0;
  bool deepOutput = false; // deep source (>8 bits/sample) -> Bgra64 instead of 8-bit reduction
};
class IDecoder {
 public:
  virtual ~IDecoder() = default;
  [[nodiscard]] virtual const ImageMeta& meta() const = 0;
  [[nodiscard]] virtual Result<FrameTiming> frameTiming(std::uint32_t frame) const = 0;
  // Decodes frame `frame` and converts it to `format` (Bgra64 uses 16-bit little-endian samples)
  // into `dst` with `pitchBytes` per row (rows top-down, coded size).
  // dst.size() must be ≥ pitchBytes * height, else Internal.
  [[nodiscard]] virtual Result<void> decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                                 std::span<std::byte> dst, std::uint32_t pitchBytes) = 0;
};
class IDecoderFactory {
 public:
  virtual ~IDecoderFactory() = default;
  [[nodiscard]] virtual bool recognises(std::span<const std::byte> head) const = 0;   // signature only
  [[nodiscard]] virtual Result<std::unique_ptr<IDecoder>> create(std::span<const std::byte> file,
                                                                 const DecoderOptions&) = 0;  // parses
};
struct ImageDescription { std::string formatName, compression, comments; };
class IImageDescriber {
 public:
  virtual ~IImageDescriber() = default;
  [[nodiscard]] virtual ImageDescription describe(const ImageMeta& meta) const = 0;
};
```

`ImageMeta::hasIcc` records only whether the container carries an ICC profile: the adapter sets it
from the codec library's own presence check (libavif's parsed `image.icc`; RPGMVP's
`spng_get_iccp` result) without retaining the profile bytes. The host ignores ICC profiles (proven,
[`docs/host/pictureview-abi.md`](host/pictureview-abi.md)), so there is no colour-management
consumer to retain them for; ICC bytes are not part of the PVD boundary.

Decisions (Task 7, open point 1):

- `Transforms` stays a shared concept and `Transform` stays in `src/core`: `Transforms{}` is a
  valid "none", HEIF-family formats all carry clap/irot/imir, and a format without them simply
  leaves the struct empty so `FileSession` never calls `Transform::apply`. No transform step is
  injected.
- `exifOrientation` is normalized to 0 or 1..8 by a codec adapter. `FileSession` maps it to the
  host's orientation code only when `Transforms{}` is empty, so pixels are never transformed by
  both the shared kernel and PictureView. An AVIF adapter ignores EXIF orientation whenever `irot`
  or `imir` is present, following their MIAF precedence.
- `chroma` and `cicp` stay plain (non-optional) fields. They are the ISO/IEC 23091-2 signalling of
  the coded samples, and CICP itself has a spelling for every case: an RGB format reports
  `Yuv444` (no subsampling) with `cicp.matrix = 0` (identity) and `fullRange = true` — for sRGB
  PNG that is exactly `{1, 13, 0, true}`, for unknown colour `{2, 2, 0, true}` — and greyscale is
  `Yuv400`. The shared colour-presentation module reads primaries and transfer; matrix and range
  remain informational because the codec adapter has already produced full-range RGB. A plugin's
  describer is free to print the signalling and presentation decision.
- The host-facing words (`formatName`, `compression`, `comments`) are produced by the plugin's
  `IImageDescriber`; `CodecPlugin` derives `pageCount` and `animated` from `ImageMeta` and
  assembles `ImageInfo`. An interface rather than a callback because everything else injected into
  core is an interface held by reference, and because the fake in `tests/core/Fakes.hpp` can then
  record what it was asked to describe.

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
- `colour` (`src/core/colour/`): a float presentation pipeline over BGRA64.
  `Transfer` implements the H.273 sRGB, BT.1886, gamma 2.2/2.8, linear, PQ and HLG functions;
  code 2 and unknown transfers fall back to sRGB. `Primaries` derives the H.273 RGB-to-BT.709
  matrices at compile time from chromaticities, using Bradford adaptation where the white is not
  D65; code 2 and unknown primaries are identity. `ToneMap` applies the BT.2390 Hermite-knee EETF
  to maxRGB so one ratio scales all three channels and preserves hue. `Presentation` caches a
  65,536-entry input-transfer LUT for its session (it depends on the CICP transfer code), applies
  HLG's 1000-nit/1.2-gamma OOTF where needed, converts the primaries, tone-maps PQ/HLG to the
  100-nit/0.005-nit SDR reference display, applies the exact sRGB OETF, clamps, rounds, and
  preserves alpha. The sRGB output step is `SrgbOutputTables::quantize`: 65,535 exact decision
  thresholds (the first float the OETF-plus-`lround` path maps to each code, found by walking
  `nextafter` from the inverse OETF of the decision boundary) narrowed by a 65,536-bucket index
  built in one merge pass over the sorted thresholds; no OETF value is ever interpolated, and the
  result is proven equal to the former per-pixel `pow` path over every float in [0, 1] on both
  architectures (`tests/core/colour/PipelineTests.cpp`, the skipped exhaustive diagnostic). Those
  tables depend on no `Cicp`, so the composition root builds one `SrgbOutputTables` per plugin
  instance (in `pvdInit`, ≈ 5 ms on x64 / 15 ms on x86 in Release) and every `Presentation` of that
  plugin borrows it by `const&` through `CodecPlugin` and `FileSession` (§7) instead of building
  384 KiB per session; `Presentation::outputTables()` and `FileSession::outputTables()` expose the
  borrowed instance so the tests can pin the sharing. PQ uses `masteringPeakNits` clamped to 10,000
  nits or 1,000 nits when absent/invalid; HLG always uses its 1,000-nit reference display.
  Identity is primaries 1 or 2 plus transfer 1, 2, 6, 13, 14 or 15. It is short-circuited so the
  existing SDR byte stream is unchanged; conversion is selected for any other primaries or
  transfer 4, 5, 8, 16 or 18. Unknown codes never reject a viewer input and are named as fallbacks
  in the plugin's image comments. `Presentation::applyImage` converts a whole tightly packed
  BGRA64 image: below 256 Ki pixels on the calling thread, otherwise in
  `min(clamp(maxThreads, 1, 4), height)` disjoint row bands (`maxThreads == 0`, the
  `DecoderOptions` default, means one band), the last band on the calling thread and the others
  on `std::thread` workers that a scope-bound joiner (`BandWorkers`, private to `Pipeline.cpp`)
  joins before the call returns, on the normal path and while unwinding after a failed thread
  start. `std::jthread` is not used: its `stop_token` state waits and notifies on atomics, for
  which MSVC ≥ 14.50 imports `api-ms-win-core-synch-l1-2-0.dll` (§7). `Presentation` is immutable
  after construction and `apply` is `const noexcept`, so the workers share it safely and no
  exception can cross a thread boundary.
- `Transform` (`src/core/Transform.hpp/.cpp`): pure functions over `PixelView`
  (`std::span<const std::byte>`, width, height, bytesPerPixel, pitch, the four integers defaulted
  to 0; always passed by `const&`):
  `Result<CropRect> validatedCrop(...)`, `std::pair<uint32,uint32> displaySize(const ImageMeta&)`,
  `bool hasTransforms(const Transforms&)`,
  `Result<PixelBuffer> apply(const Transforms&, const PixelView&, std::uint64_t maxPixels)` applying
  **clap → irot → imir** in that order; with no transform present `apply` returns an identity copy
  (no precondition, never throws for a well-formed view). There are no separate public
  `crop`/`rotate`/`mirror` entry points: `apply` with a single property set is the per-operation
  interface, and the tests pin each operation that way. `PixelBuffer::create(width, height,
  bytesPerPixel, maxPixels)` performs the `maxPixels`/overflow check itself and returns `TooLarge`.
  Order (AVIF spec §"Transformative properties"; cross-check the comment on `transformFlags` in the
  installed `avif/avif.h` and cite both in a code comment). `irot` angle n = n × 90° anti-clockwise.
  `imir` semantics follow the installed `avif.h` comment for `axis`; the adapter maps the number to
  `MirrorAxis`. Unit tests use 2×3 / 3×2 synthetic images with hand-derived expected outputs.
- `CodecPlugin : pvd::IPlugin` (`src/core/CodecPlugin.hpp/.cpp`), ctor
  `(IFileSource&, IDecoderFactory&, const IImageDescriber&, const colour::SrgbOutputTables&,
  const DecoderOptions&, PluginInfo)` (the options are copied into the member; `FileSession` takes
  them the same way; the tables are borrowed and handed to every session).
  `open()`: `recognises(head)` else `NotRecognised`; if `fileSize == 0` data = `head`, else
  `fileSource.open(name)` → `IFileData` owned by the session; `factory.create(data, options)`;
  `describer.describe(meta)`; build `ImageInfo{frameCount, animated, formatName, compression,
  comments}`; return `FileSession`. It is the same class for every plugin.
- `FileSession : pvd::IFileSession` (`src/core/FileSession.hpp/.cpp`): owns
  `std::unique_ptr<IFileData>` (null in memory mode), `std::unique_ptr<IDecoder>`, `ImageInfo`,
  `std::vector<std::unique_ptr<PixelBuffer>> outstanding_`, `DecoderOptions`, the borrowed
  `const colour::SrgbOutputTables&` (ctor parameter, exposed by `outputTables()`), and one immutable
  `Presentation` over those tables for the session when colour conversion is needed.
  - `pageInfo(p)`: range check; `displaySize(meta)`;
    `bitsPerPixel = indexed ? depth : depth × (hasAlpha ? 4 : 3)` (informational, so indexed4
    reports 4 and 10-bit RGBA reports 40); `frameTimeMs` = `frameTiming(p)` when animated else 0.
    EXIF orientation does not alter these dimensions because PictureView applies that rotation.
  - `decodePage(p, progress)`: range check; `progress.report(0, 3)`; allocate `PixelBuffer` at coded
    size (`Bgra64` with 8 bytes/pixel when colour presentation is needed, or when
    `deepOutput && depth > 8`; otherwise `Bgra32` if `hasAlpha`, else `Bgr24`); `decodeFrame`;
    `Presentation::applyImage` in place before any geometric transform, with
    `options_.maxThreads` as its band budget (so images below 256 Ki pixels stay on the calling
    thread and larger ones use `min(clamp(maxThreads, 1, 4), height)` row bands, `maxThreads == 0`
    meaning one band, as described under `colour` above);
    propagate `meta.hasAlpha` independently of layout; `progress.report(1, 3)`;
    if any transform present → `Transform::apply` into a new buffer; `progress.report(2, 3)`;
    move buffer into `outstanding_`; return the view with the EXIF-to-PictureView orientation code,
    gated off whenever a shared transform is present. Any `false` from `report` → `Aborted`.
  - `freePage(span)`: erase the buffer whose `bytes().data() == span.data()`; return whether found.
- A plugin's composition root (`plugins/<id>/src/DefaultPlugin.cpp`) defines `pvd::makePlugin()`
  returning an object that owns `win::FileSource`, the plugin's `IDecoderFactory`, its
  `IImageDescriber`, the `core::colour::SrgbOutputTables` and a `core::CodecPlugin` (declared in
  that order) and forwards `IPlugin`.
  It chooses the `DecoderOptions` and builds `PluginInfo` from `kPluginIdentity` plus run-time
  library versions. Every production composition sets `deepOutput = true`, so sources deeper than
  8 bits are delivered as BGRA64 without a build switch; 8-bit-and-shallower sources keep their
  BGR24/BGRA32 layouts unless colour presentation requires BGRA64 headroom. Plugin-specific values
  are recorded in each `plugins/<id>/DESIGN.md`.

### 3.8 Shared adapter

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
- The `file` span given to an `IDecoderFactory::create` must outlive the decoder (codec libraries
  keep pointers into it); `FileSession` guarantees this by declaring the file data before the decoder.
- Codec adapters are one-to-one over their library and contain no decisions; the AVIF one is
  described in `plugins/avif/DESIGN.md`.

## 4. Build

- CMake ≥ 3.28, presets in `CMakePresets.json`: `debug`, `release`, `coverage`, `asan` (x64) and
  `debug-x86`, `release-x86`, `coverage-x86` (32-bit; all Ninja, clang-cl, lld-link). The names
  are unchanged from v1 (`asan` is Task 10's); every preset builds every plugin.
  `-DPVDKIT_PLUGINS=<id>[;<id>]` restricts the build (and the vcpkg manifest features, see below)
  to those plugins. The x86 presets differ from the x64 ones only in
  `VCPKG_TARGET_TRIPLET = x86-windows-static-clang`; `cmake/vcpkg-root.cmake` reads
  `triplets/<triplet>.cmake` to pick the chainload toolchain (`cmake/clang-cl-x86.toolchain.cmake`:
  the same x64-hosted clang-cl cross-compiling with
  `CMAKE_<LANG>_COMPILER_TARGET = i686-pc-windows-msvc`), so the triplet file is the one place
  that maps an architecture to its toolchain. The x86 triplet also sets `VCPKG_LOAD_VCVARS_ENV ON`:
  vcpkg does not load vcvars for chainloaded triplets, and without it meson (dav1d) activates
  `vcvars64.bat` itself and links i686 objects against the x64 CRT (`_mainCRTStartup` unresolved).
  A plugin lands in `build/<preset><suffix>/plugins/<id>/<NAME>.pvd` (x86 builds are
  `IMAGE_FILE_MACHINE_I386`).
  `release`: `/O2`, `/MT`, `/DEBUG:NONE`. `coverage`: `/Od /Zi /MT`
  + `-fprofile-instr-generate -fcoverage-mapping -fprofile-update=atomic` on every target - the
  atomic counter updates because the leak tests decode through the instrumented DLL on eight
  threads at once, and plain adds lose each other (a probe recorded 1.4-3.0 M of 8 M increments
  without the flag, exactly 8 M with it; the x86 gate once read 99.657 % branches for that
  reason).
- Common flags: `/clang:-std=c++23 /W4 /WX /permissive- /utf-8 /EHsc /Zc:preprocessor /guard:cf`
  (Control Flow Guard on the compile and the link line of every configuration, BinSkim BA2008; it
  adds no import) plus `-Wno-` nothing unless justified. `static_assert(__cplusplus >= 202302L)` in `src/core/Error.hpp`.
  The `coverage-x86` build links `clang_rt.profile-i386.lib` from the x86-hosted LLVM of the same
  Build Tools explicitly (the x64-hosted one ships only the x86_64 runtime, so clang names an
  arch-less `clang_rt.profile.lib` that exists nowhere; `/NODEFAULTLIB` drops that name).
- Resources: `project(... LANGUAGES CXX RC)`; `src/pvd/Plugin.rc` is compiled by llvm-rc (from the
  chainload toolchain) once per plugin and linked into its DLL; it adds no imports (`check_imports`).
- vcpkg manifest `vcpkg.json`: `doctest` for the kit, and one **feature per plugin id** carrying
  that plugin's libraries (`avif` → `libavif[dav1d]`, which pulls `libyuv`); `default-features`
  lists every plugin, and `cmake/vcpkg-root.cmake` maps `PVDKIT_PLUGINS` to
  `VCPKG_MANIFEST_FEATURES` (+ `VCPKG_MANIFEST_NO_DEFAULT_FEATURES`) so a restricted build installs
  only what it needs. Custom triplets `triplets/x64-windows-static-clang.cmake` /
  `x86-windows-static-clang.cmake` = static + chainload `cmake/clang-cl(-x86).toolchain.cmake`
  so every port gets the same toolchain (and libyuv its SIMD paths, vcpkg#28446). The chainload
  toolchain sets only compilers, linker, resource compiler and `CMAKE_MSVC_RUNTIME_LIBRARY`; the
  overlay port `ports/libavif` (registered through `VCPKG_OVERLAY_PORTS`) carries the clang-cl
  fix for libavif's static-library merge, see `plugins/avif/DESIGN.md`. Changing a toolchain file
  changes every port's ABI hash, i.e. rebuilds the ports once.
- Targets. Shared: `pvdkit_options` (INTERFACE flags), `pvdkit_core`, `pvdkit_pvd`,
  `pvdkit_win` (all static). Per plugin, from `cmake/pvdkit-plugin.cmake`:
  - `pvdkit_plugin_identity(<id> ...)` → `<id>_identity` (INTERFACE, generated header, §3.4a);
  - the plugin's own `add_library` calls: `<id>_core` (static, `src/core/**`), `<id>_adapter`
    (static, `src/adapters/**`, links the codec ports), `<id>_composition` (static,
    `src/DefaultPlugin.cpp`, links the two plus `pvdkit_win`, `pvdkit_pvd`, `<id>_identity`);
  - `pvdkit_add_plugin(<id> LINK <id>_composition LICENSES <port> <name> ...)`
    → `<id>_plugin` (SHARED; `OUTPUT_NAME <NAME>`, `SUFFIX .pvd`, `PREFIX ""`; sources =
    `Exports.cpp` + `Plugin.def` + `Plugin.rc` read from properties of `pvdkit_pvd`) and the
    package staging `<bindir>/package/` (static `readme_en.txt`, `readme_ru.txt` and `ChangeLog`
    copied byte-for-byte after configure-time identity/encoding/line-ending checks,
    `LICENSES.txt` assembled from vcpkg's `share/<port>/copyright` files, `manifest.json` = name,
    version, architecture, file name); `<id>_package_docs` repeats the document checks on the
    staged files under CTest;
  - tests: `<id>_core_tests`, `<id>_adapter_tests` (the plugin's own `add_executable`), and
    `pvdkit_add_plugin_e2e_tests(<id> FIXTURES <dir> SOURCES ...)` → `<id>_e2e_tests` (in
    coverage builds with the ctest `ENVIRONMENT` property
    `LLVM_PROFILE_FILE=<build>/pvdkit-<id>-%p-%m.profraw`, so the coverage gate can tell this
    DLL's profile from every other plugin's) plus the ctest entries `<id>_check_imports` and
    `<id>_check_exports` (Release configuration), and - from the same call, no extra line in the
    plugin - `<id>_leak_tests` (the shared leak scenarios over the same fixtures, §5).
  Shared tests: `pvd_tests` (compiles `Exports.cpp` against `pvd_tests_identity`), `core_tests`,
  `adapter_tests` (win), `guard_tests`. Per-directory `CMakeLists.txt` with
  `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`; the top level globs `plugins/*/CMakeLists.txt` the
  same way, so a new plugin is a new directory and nothing else.
- `scripts/check-imports.ps1`: fails unless the import table of the given DLL is exactly `KERNEL32.dll`
  (`llvm-readobj --coff-imports`). `scripts/check-exports.ps1`: exactly the eight bare `pvd*` names.
  Both are registered per plugin as ctest tests in the release presets.
- `scripts/lint.ps1 -BuildDir <configured build> -ReleaseDir <release build> [-Tools ...] [-Jobs n]`
  (Task 9): the static-analysis gate, run once per architecture and not part of ctest. clang-format
  (`--dry-run --Werror`, `.clang-format`: Microsoft base, 120 columns, 4 spaces) over `src/**`,
  `plugins/*/src/**`, `tests/**`, `plugins/*/tests/**`; clang-tidy (`.clang-tidy`:
  `clang-analyzer-*, bugprone-*, performance-*, portability-*`, warnings as errors, headers under
  `src/`, `tests/` and the plugin `src/`/`tests/` roots reported) over every TU of the build's
  `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS` is on in the base preset), `-Jobs`
  processes at a time (half the cores by default), with `tests/.clang-tidy` and
  `plugins/<id>/tests/.clang-tidy` relaxing `bugprone-unchecked-optional-access`,
  `clang-analyzer-optin.core.EnumCastOutOfRange`, `bugprone-random-generator-seed` and
  `bugprone-bitwise-pointer-cast` for test code only, and the root file disabling `portability-avoid-pragma-once` (Task 24 fix round: the
  CI `lint` job runs the runner's clang-tidy, 22 on the Windows Server 2025 image, whose new
  checks the reference machine's 19 never sees - such a check is either satisfied by the code or
  disabled with its reason in the `.clang-tidy` files, and both the local run and the CI run must
  be clean; the local run alone cannot prove the CI one); cppcheck
  (`--enable=warning,performance,portability --std=c++23 --platform=win64|win32W --library=windows`,
  the same compile database restricted to `src/` and `plugins/*/src/`, `cppcheck-suppressions.txt`);
  PSScriptAnalyzer (`scripts/`, `plugins/*/scripts/`, the root `.psd1` files;
  `PSScriptAnalyzerSettings.psd1`, no exclusions); BinSkim over every release `<NAME>.pvd`
  (`binskim.psd1`: error and warning results fail - SARIF warnings carry no `level`, the script
  applies the 2.1.0 default; the PDB-less release build leaves the PDB rules unevaluated and
  Spectre, CET shadow stack and `/sdl` deliberately undone, as documented there) plus the
  `/HIGHENTROPYVA` bit of every 64-bit image read with llvm-readobj (BinSkim's BA2015 does not
  apply to DLLs). Every tool runs at below-normal priority. Exit 1 on any finding; every
  suppression carries its reason in the file it lives in or next to the `NOLINT`.
- Build directories: `build/<preset>$env{PVDKIT_BUILD_SUFFIX}` so several agents can build the
  same tree concurrently without sharing a Ninja/CMake state directory.
- `scripts/coverage.ps1 [-Preset coverage|coverage-x86]`: configures+builds the preset, runs every
  ctest executable under `LLVM_PROFILE_FILE` (`pvdkit-%p-%m.profraw`; each `<id>_e2e_tests`
  overrides it through its ctest `ENVIRONMENT` property, set by `pvdkit_add_plugin_e2e_tests` in
  coverage builds, with `pvdkit-<id>-%p-%m.profraw`), merges with `llvm-profdata`, runs
  `llvm-cov report` over the test executables **and every `*.pvd` under `build/.../plugins`**
  with `-ignore-filename-regex` excluding `tests/`, `third_party/`, `vcpkg`, requires that each DLL
  wrote its own profile — a check per plugin id: among the `pvdkit-<id>-*` files only, a process
  that left two raw profiles whose merge shows `Exports.cpp` functions executed in that DLL's
  mapping; `Exports.cpp` is identical in every plugin DLL, so without the id another plugin's
  e2e process would satisfy the mapping just as well — requires every `.cpp` / executable `.hpp`
  under `src/**` and `plugins/*/src/**` to appear in the report, prints the totals, writes HTML
  to `build/<preset><suffix>/html`, exits 1 unless lines == 100 % and branches == 100 %. The same
  gate applies to both presets. It is a whole-tree gate: run it with every plugin enabled.
- `scripts/build-all.ps1`: builds `release` and `release-x86`, discovers every plugin through its
  `package/manifest.json`, runs both gates on each DLL and copies them to `dist/x64/<NAME>.pvd`
  and `dist/x86/<NAME>.pvd`. `scripts/pack.ps1` (Task 23): from existing `release` /
  `release-x86` build directories (`-Suffix`, optional `-Plugins` ids, `-DistDir`), no build, runs
  both gates on each DLL, checks its `FileVersion` against the manifest, then writes
  `<DistDir>/<NAME>-<version>-{x64,x86}.zip` (`<NAME>.pvd`, `readme_en.txt`, `readme_ru.txt`,
  `ChangeLog`, `LICENSES.txt`; `manifest.json` is build metadata and stays out) with their
  SHA-256, overwriting only same-named zips. `scripts/package.ps1`: the whole pipeline from
  scratch (suffix `-pkg`): `build-all.ps1 -Clean`, `lint.ps1` on both release directories, the
  `asan` preset from scratch (`build/asan-pkg`: configure, build, `ctest --preset asan`; any
  AddressSanitizer report fails the packaging), then `pack.ps1` into `dist/`. The GitHub
  workflows (`.github/workflows/`, Task 23; `docs/BUILD.md` "Continuous integration and releases")
  run the release builds (each build job first prints the MSVC toolset directories and the
  clang-cl version it builds with, so an import-table failure is attributable to a toolset at a
  glance - Task 24), `ctest`, `lint.ps1` and `coverage.ps1` on `windows-latest` and, on a
  `<id>/vX.Y.Z` tag, `pack.ps1` plus `gh release create` and the README "Downloads" row update.
  The toolchain files resolve the LLVM directory through `cmake/find-llvm.cmake`
  (`PVDKIT_LLVM_DIR`, the known VS 2022 layouts, then any Visual Studio major and edition under
  either Program Files root - the glob the toolchain action uses and what the VS 2026 runner image
  resolves to - then PATH; `scripts/llvm-dir.ps1` mirrors it for the scripts), so the reference
  machine's Build Tools path is a default, not a requirement.
- Preset `asan` (Task 10, level 2; x64 only): `CMAKE_BUILD_TYPE=RelWithDebInfo` with
  `PVDKIT_ASAN=ON`, which adds `-fsanitize=address /Od /Zi` to every target - the plugin DLLs
  included - and `/DEBUG` at link time, so a report names the line. RelWithDebInfo rather than
  Release keeps `/MT` and the release ports (vcpkg maps the configuration) without the
  Release-only gates (`check_imports`, `check_exports`) that the thunk model would not satisfy in
  spirit. CMake links with lld-link directly, so the top-level `CMakeLists.txt` names the runtime
  libraries clang-cl's own driver would add (LLVM 19 `lib/clang/19/lib/windows`): an executable
  gets `clang_rt.asan-x86_64.lib` and `clang_rt.asan_cxx-x86_64.lib` as `/wholearchive:` inputs, a
  DLL gets `clang_rt.asan_dll_thunk-x86_64.lib` (it forwards to the executable's runtime through
  `GetProcAddress`, so the instrumented `.pvd` still imports only `KERNEL32.dll` and exports the
  eight names) - the `197611TARGET_PROPERTY:TYPE>` of the target being linked selects the set. The
  test preset sets `ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:allocator_may_return_null=0`;
  `detect_stack_use_after_return` stays off (its fake stack is incompatible with SEH-based C++
  unwinding here and wants 11 MB per thread). Three toolchain facts, each verified with a probe on
  clang-cl 19.1.5 / UCRT 10.0.26100: **LeakSanitizer does not exist on Windows** - the runtime
  answers `AddressSanitizer: detect_leaks is not supported on this platform` and exits 1, so the
  leak gate is the level-1 accounting of `<id>_leak_tests` and this preset is the memory-error
  gate only; the x86 cross build is not offered (the x64-hosted LLVM ships only the x86_64
  runtime, and LSan would be absent there too); and ASan instrumentation breaks a rethrow
  (`throw;`) inside a catch handler (garbage exception object, access violation or fail-fast
  0xC0000409), which only doctest's exception translation does - `tests/TestMain.cpp` therefore
  wraps the doctest implementation in `#pragma clang attribute push(no_sanitize("address"))`
  under ASan; nothing under `src/**` or `plugins/*/src/**` rethrows (the firewall swallows). At
  start-up the runtime also prints `interception_win: unhandled instruction` for one CRT function
  it cannot hot-patch in this UCRT; instrumented code is checked regardless (the probe caught a
  one-byte heap overflow in an EXE and in a `LoadLibrary`-ed DLL with full symbols). Under ASan
  `HeapAlloc` of every module is served by ASan's allocator and freed memory is quarantined, so
  the leak scenarios check handles and the mapped-view count only there (§5) and print the rest.
  What is and is not instrumented: our code (`src/**`, `plugins/*/src/**`, the tests) is; the vcpkg ports -
  libavif, dav1d, libyuv - are not (they are built by vcpkg without `-fsanitize=address`), so
  inside them only ASan's interceptors see anything (`malloc`/`free`/`memcpy`/... arguments, a
  use after free of a block, an overflow that reaches a redzone through an intercepted call),
  not an out-of-bounds read by dav1d's own instructions. The hostile-corpus claim under this
  preset is therefore about our code on hostile input plus the host-side check that every byte
  of every handed-out page is readable; a bug confined to a port's own instructions is caught by
  the port's upstream fuzzing, not here.

## 5. Tests

- Framework: doctest (vcpkg). Shared executables: `pvd_tests`, `core_tests`, `adapter_tests`,
  `guard_tests`; per plugin: `<id>_core_tests`, `<id>_adapter_tests`, `<id>_e2e_tests`. Parallel
  agents own disjoint directories.
- `tests/pvd`: `Firewall`, `ContextHandle`, `Shim` (fake `IPlugin`/`IFileSession`: success, every
  `ErrorCode`, throwing, null host pointers, callback abort; the identity fallback with a test
  `PluginIdentity`), `Progress`, `Exports.cpp` with a test `makePlugin()` and the generated
  `pvd_tests` identity (42 / `TestPlugin` / 9.8.7).
- `tests/core`: `PixelBuffer`, `Transform` (all angles, both axes, crop, combined, invalid crop),
  `CodecPlugin` and `FileSession` on fake `IFileSource`/`IDecoderFactory`/`IDecoder`/`IImageDescriber`
  (memory mode vs file mode, open failure, parse failure, page range, decode failure, conversion
  failure, abort at each step, transforms present/absent, alpha/no alpha, animated timing,
  freePage known/unknown, size limit, the describer's words and the meta it receives, exception
  propagation from every collaborator).
- `tests/adapters`: `FileMapping`/`FileSource` on temp files (normal, empty, missing, non-ASCII
  name, long path).
- `tests/e2e`: not an executable but the shared host driver (`PluginHost.hpp/.cpp`: `LoadLibraryW`,
  `GetProcAddress` of all eight, open in disk and memory mode, decode helpers) and
  `VersionResourceTests.cpp` (VERSIONINFO vs generated identity vs `pvdPluginInfo`), compiled
  into every plugin's `<id>_e2e_tests` with `PVDKIT_PLUGIN_PATH` / `PVDKIT_FIXTURE_DIR` set for
  that plugin. The plugin adds its own `E2eTests.cpp` (fixture expectations, pixel checks, the
  rejection list, concurrency).
- `tests/support`: the leak gate (Task 10, level 1; every preset, both architectures). Static
  library `pvdkit_leakcheck` = `LeakCheck.hpp/.cpp` (the accounting) + `HostileCorpus.hpp/.cpp`
  (the fuzz-lite corpus generator), with `leakcheck_tests` as its self-test against deliberately
  leaking fakes (a leaked block and a leaked handle are charged exactly, warm-up allocations are
  not; the corpus is deterministic and every mutant differs from its source); `leak/LeakScenarios.cpp`
  is compiled into every plugin's `<id>_leak_tests` by `pvdkit_add_plugin_e2e_tests` together with
  the host driver (`PVDKIT_PLUGIN_PATH` / `PVDKIT_FIXTURE_DIR` as for the e2e test), so a plugin
  registers nothing extra. Accounting (decision, Task 10): the process heap is walked (`HeapWalk`
  under `HeapLock`, busy blocks and their bytes) rather than `_CrtMemCheckpoint`, because every
  module built with the static CRT has its own CRT instance whose debug-heap bookkeeping is
  invisible to the others - the test executable could never see the plugin DLL's blocks - while
  every UCRT instance, dav1d's `_aligned_malloc` and libyuv's `malloc` all draw from the one
  process heap, so one mechanism serves Debug and Release and both architectures (verified: a
  `malloc`'d block shows up once, LFH activation mid-run changes nothing); plus
  `GetProcessHandleCount`; plus the mapped views - the committed `MEM_MAPPED` regions of the
  address space, enumerated with `VirtualQuery` (count and bytes) - because a view of a file
  mapping is neither a heap block nor a handle and, being file-backed, hardly moves the commit
  charge: a `FileMapping` whose `UnmapViewOfFile` was made a no-op passed every other counter
  (200 leaked views: blocks +0, handles +0, private +24 KiB) and is caught by this one (views
  +200, +800 KiB); image sections and private memory are not counted, so the view count is stable
  under ASan too, while the bytes are not (ASan's runtime grows its own `MEM_MAPPED` regions in
  place: views +0, +24 KiB measured), so under ASan the gate checks handles and the view count
  only and prints the rest; and, as the coarse cross-check the task asked for, the commit charge
  (`GetProcessMemoryInfo`, `PrivateUsage`; it wanders by up to +-9 MiB on its own and not with the
  iteration count, so it is gated at 32 MiB and printed next to the heap's committed size). One
  process-heap block is Windows' own and is recognised rather than charged: ntdll allocates a
  critical section's `RTL_CRITICAL_SECTION_DEBUG` (48 bytes on x64, 32 on x86) on its first
  contended acquisition - from a static pool of 64, then from the process heap - and keeps it
  while the section lives (`DeleteCriticalSection` zeroes it and keeps it for reuse; all of this
  measured on this machine, both architectures); the plugin DLL's UCRT locks (`__acrt_locale_lock`,
  `__acrt_multibyte_cp_lock`) are first contended when several threads start using its CRT at
  once, which can happen in the warm-up, the first pass or the second. A new block of exactly
  that size, `Type` 0, whose `CriticalSection` field points at a readable `CRITICAL_SECTION` whose
  `DebugInfo` points back at the block is therefore taken out of the heap counters and reported
  as `cs-debug +n` (self-tested: a fresh section contended after exhausting the static pool is
  recognised and not charged; a same-sized ordinary block, zeroed or pointing elsewhere, is
  charged). A snapshot waits until two readings 5 ms apart agree on every number (the kernel
  releases a joined thread's stack and decommits pages a little after the fact; if they never
  agree within 1 s the log says `snapshot did not settle`) and records every busy block
  (address, size, first 16 bytes) in one of two pre-reserved buffers, sorted by address, so
  growth is reported as a list of the blocks that appeared rather than as a bare count. `measureLeaks(scenario, warmUp, N, body)` runs the
  body `warmUp` times (one full cycle over the fixtures for cycling scenarios, the hostile
  corpus included: lazy one-time allocations of the loader, the CRT and the codec libraries are
  not leaks), snapshots, runs it N times, snapshots; a pass that shows growth is followed by a
  second measured pass from a fresh snapshot and both are printed, but the second one decides
  only when the first grew by no more than the known noise (`kRetryNoise`: 2 blocks, 1 KiB, no
  handle, no view - the zeroed debug block ntdll keeps when a section is deleted right after its
  first contention within a pass, which the recognition can no longer claim; a joined thread
  itself leaves nothing: 30 spawn/join cycles measured at 0 blocks), otherwise the first pass
  stands and the gate fails: a cache that fills after the warm-up (+1 block of 64 KiB, injected
  in a scratch copy) is reported as a finding, not retried away. `requireNoLeak` demands no
  growth in blocks, bytes, handles, views and view bytes (a negative delta is a release of
  something warm-up allocated, never a leak) and prints one `[leak] <scenario>: heap blocks +n,
  heap bytes +n, handles +n, views +n (+n KiB), cs-debug +n, private +n KiB (...) (warm-up w x
  ms, measured N x ms)` line, with both passes appended when a second one ran. N = 200
  host-level operations per scenario
  (`PVDKIT_LEAK_ITERATIONS` overrides). Scenarios (`docs/tasks/task10-leaks.md`): disk and memory
  round trips with `pvdInit`/`pvdExit` inside the loop; close without free; decode aborted at
  callback step 0, 1 and 2; every rejection path (each rejected fixture in both modes, empty and
  11-byte heads, a valid head for a missing file, pages out of range); two pages outstanding freed
  in both orders; N sessions open at once then closed (every 8th with a page, half of those freed
  before the close); `pvdInit`/`pvdExit` cycled N times; 8 threads x N/8 round trips; `LoadLibrary`/
  `FreeLibrary` cycled 20 times with a round trip inside (in coverage builds with an explicit
  allowance of 1 block, 256 bytes and 2 handles per load for the LLVM profile runtime inside the
  instrumented DLL, `PVDKIT_COVERAGE`; zero everywhere else); host-like browsing with a sliding
  window of 3 open files over the whole folder, N/24 passes, a decode aborted every 5th file, a
  page switch on every animated file, every other close with pages still out; and the hostile
  corpus - every rejected fixture plus 200 mutants of the accepted ones (`Mutation`: flip,
  truncate, flip+truncate, box size, splice, trailing garbage, zero range; seed 20260912; written
  to `%TEMP%/pvdkit-hostile-<pid>-<seed>-<n>/` and removed afterwards) opened in both modes, each
  refused or decoded to a page whose every byte is read. Fixtures are discovered through the DLL
  itself (every regular non-`.md` file under the fixture directory, opened once in both modes:
  accepted with its page count, or rejected); a plugin must ship at least one of each. Unit-level
  counterparts: `tests/adapters/LeakTests.cpp` (`FileMapping`/`FileSource` open and close, every
  failure path) and `plugins/<id>/tests/adapters/LeakTests.cpp` (decoder create/decode/destroy on
  every fixture with the error paths, refusals, the composed plugin). Cost: Task 10's "< 30 s per
  architecture" budget applied to the plain presets, and `avif_leak_tests` met it on an unloaded
  machine until Task 24: ~15 s in Release, ~24-27 s in Debug, x64 and x86 alike. Since Task 24
  every `pvdInit` builds the plugin's `SrgbOutputTables` (§7: ≈ 5 / 12 ms per call on x64
  Release / Debug, ≈ 15 / 25 ms on x86), and the scenarios that cycle `pvdInit` about 650 times
  (the disk and memory round trips, init/exit cycle, LoadLibrary/FreeLibrary) pay ≈ 650 × that:
  the Debug budget is no longer met on either architecture (unloaded: Debug x64 ≈ 50 s, measured
  by the Task 24 review; Debug x86 ≈ 45 s by extrapolation), and Release is close to it (unloaded
  Release x64 ≈ 23 s, Release x86 ≈ 33 s, both measured by the review). The delta itself was
  measured on a loaded machine, HEAD against Task 24 back to back: `avif_leak_tests` 46 s → 64 s
  in Debug x86, the init/exit cycle alone 1 ms → 2.5 s in Debug x64. A test-time cost only, since
  the host calls `pvdInit` once per plugin load; the lazy build behind a `std::mutex` that §7
  mentions would remove it. The instrumented builds run the same N and are deliberately not
  trimmed (a smaller N there would test less of exactly the build the gate is instrumented for):
  ~30-42 s under `coverage`, ~30 s under `asan` before Task 24, ≈ 54 / 59 s after (x64, loaded).
- `tests/guard`: walks `src/` and every `plugins/*/src/` and fails on forbidden tokens: `new `,
  `new(`, `delete `, `malloc`, `calloc`, `realloc`, `free(`, `shared_ptr`, `weak_ptr`,
  `reinterpret_cast` outside adapters/pvd, `#include <windows.h>` outside `src/adapters/**`,
  `plugins/*/src/adapters/**`, `src/pvd/Exports.cpp` and `src/pvd/PvdApi.hpp` (the same set
  AGENTS.md rule 4 names), codec headers (`avif/avif.h`, `dav1d/dav1d.h`; extend the list with
  each plugin's library) outside those adapters and `Exports.cpp`, `catch (`
  outside `Firewall.hpp`, `LCOV_EXCL`, `__builtin_unreachable`, `[[assume`, and (Task 24, AGENTS.md
  rule 13) the synchronisation family behind the `api-ms-win-core-synch-l1-2-0.dll` import of
  MSVC ≥ 14.50 (§7 says which members were observed and which are forbidden by extension):
  `jthread`, `stop_token`/`stop_source`/`stop_callback`, `call_once`/`once_flag`,
  `<latch>`/`std::latch`, `<barrier>`/`std::barrier`,
  `<semaphore>`/`std::counting_semaphore`/`std::binary_semaphore`, `condition_variable(_any)`,
  `.wait(`/`.wait_for(`/`.wait_until(`/`.notify_one(`/`.notify_all(` (also `->`),
  `std::atomic_wait*`/`std::atomic_notify_*` and their `std::atomic_flag_wait*`/
  `std::atomic_flag_notify_*` twins, `timed_mutex`/`recursive_timed_mutex`/`shared_timed_mutex`,
  `<future>`/`std::future`/`shared_future`/`promise`/`async`/`packaged_task`,
  `<syncstream>`/`std::osyncstream`/`syncbuf` (`basic_` forms included), plus any function-local
  `static` that is not `static constexpr` (thread-safe statics: `_Init_thread_*`). The type
  spellings are matched after `::` so an identifier merely named `future`, `latch` or `promise`
  is not the token; a `using namespace std;` would defeat that, and none exists. For that last
  rule every brace scope
  is classified by its header - the code since the previous `;`, `{` or `}` minus access
  specifiers, attributes, `alignas` and `template <...>` heads - as declarative (`namespace`,
  `class`, `struct`, `union`, `enum`, `extern "C" {`; no parenthesis in the header) or executable
  (function bodies, control statements, lambdas, brace initialisers); `static_assert` and
  `static_cast` are different tokens. Layering rules,
  re-expressed for the two-root tree (Task 7, open point 3), the same under `src/` and under
  `plugins/<id>/src/`:
  - `core/**` includes no `pvd/` header other than `pvd/Types.hpp` and `pvd/Plugin.hpp` (allowlist);
  - `pvd/**` includes no `adapters/` header; only `pvd/Exports.cpp` includes `pvd/PluginFactory.hpp`
    or the generated `pvd/PluginConstants.hpp`;
  - `adapters/**` includes no `pvd/` header at all;
  - `plugins/<id>/src/DefaultPlugin.cpp` is the only file allowed outside the three layer
    directories, and may include `pvd/PluginFactory.hpp`, `pvd/PluginConstants.hpp`, `pvd/Plugin.hpp`,
    `pvd/Types.hpp`, adapters and core;
  - include ownership: every `#include` is resolved against an index of the headers under each
    source root, the includer's own root and the shared root first. What resolves there is the
    includer's own header — every plugin has a `core/Describe.hpp`, and each may include its own
    copy regardless of the namesakes under the other plugin roots. Only an include that resolves
    in no allowed root but does resolve under a foreign root is a violation: for shared code any
    `plugins/*/src`, for a plugin another plugin's root (includes that resolve nowhere — std, SDK,
    vcpkg — are ignored by this rule).
  Comments and string literals are stripped before matching so the rule text itself does not trip
  it. Every rule has self-tests in both polarities on synthetic paths and a fake two-plugin index.

## 6. Adding a plugin (what Task 8 does)

1. `plugins/<id>/CMakeLists.txt`: `find_package` the codec, compute the comments string,
   `pvdkit_plugin_identity(<id> NAME <NAME> VERSION x.y.z PRIORITY n DESCRIPTION ... COMMENTS ...)`,
   `add_library(<id>_core ...)`, `add_library(<id>_adapter ...)`, `add_library(<id>_composition
   src/DefaultPlugin.cpp)`, `pvdkit_add_plugin(<id> LINK <id>_composition LICENSES <port>
   "<name (licence)>" ...)`, `add_subdirectory(tests)` under `BUILD_TESTING`.
2. `vcpkg.json`: a feature `<id>` with the codec ports; add it to `default-features`.
3. `src/adapters/<lib>/`: `IDecoderFactory` (`recognises` = signature check on the head,
   `create` = parse) and `IDecoder` over the library, one-to-one, no decisions.
   `src/core/`: the `IImageDescriber` and any other decision that needs no library.
   `src/DefaultPlugin.cpp`: `pvd::makePlugin()` owning `win::FileSource`, the factory, the
   describer and a `core::CodecPlugin`.
4. `fixtures/` + `fixtures/SOURCES.md`, `tests/core`, `tests/adapters`, `tests/e2e/E2eTests.cpp` +
   `pvdkit_add_plugin_e2e_tests(<id> FIXTURES ... SOURCES ...)`, static distribution documents
   `package/{readme_en.txt,readme_ru.txt,ChangeLog}`, `README.md`, `DESIGN.md`.
5. Copy `tests/.clang-tidy` to `plugins/<id>/tests/.clang-tidy` (clang-tidy reads the nearest
   configuration above a translation unit, so the test-only relaxations do not reach a plugin's
   tests otherwise) and run `scripts/lint.ps1` on both architectures.
6. Extend the guard's codec-header list with the new library's header. Nothing under `src/`,
   `scripts/` or `CMakePresets.json` changes.

## 7. Concurrency and lifetime

- The host may decode several files at once (prefetch) and may open files from several threads;
  every session is independent, and the only process-wide state is the composition root created
  in `pvdInit` (the `std::unique_ptr<ProcessState>` in `Exports.cpp`, whose destructor is the one
  `atexit` registration a plugin DLL makes). Codec libraries must be thread-safe across decoder
  instances (libavif/dav1d are).
- No thread-safe statics, no `std::jthread`/`stop_token`/`stop_source`, no atomic
  `wait`/`notify_*` (member or free function, `atomic_flag_*` included), no
  `<latch>`/`<barrier>`/`<semaphore>`, no `call_once`/`once_flag`, no `condition_variable`, no
  `timed_mutex`/`recursive_timed_mutex`/`shared_timed_mutex`, no `<future>`, no `<syncstream>`
  anywhere in `src/**` or `plugins/*/src/**` (AGENTS.md rule 13, guard test). What was observed
  (Task 24, the first CI run): the vcruntime and STL of MSVC ≥ 14.50 (Visual Studio 2026) import
  `WaitOnAddress`/`WakeByAddressAll` directly from `api-ms-win-core-synch-l1-2-0.dll` - instead
  of the run-time lookup with a fallback that MSVC 14.44 still performs - for exactly two things:
  the guarded initialisation of a function-local `static` (`_Init_thread_wait`/
  `_Init_thread_notify`) and atomic wait/notify (`__std_atomic_wait_direct`/
  `__std_atomic_notify_all_direct`, which `std::jthread`'s `stop_token` state uses). Those were
  the two users in the tree, and both `AVIF.pvd` and `RPGMVP.pvd` failed `check_imports` on x64
  and x86 there while the same sources import `KERNEL32.dll` only on 14.44. The rest of the
  family is forbidden by extension, not by measurement: `<latch>`, `<barrier>`, `<semaphore>` and
  `<syncstream>` are built on atomic wait/notify in the STL headers; the timed mutexes and
  `<future>` on `condition_variable`/`_Cnd_t`; `condition_variable` and `call_once` are
  `SleepConditionVariableSRW`- and `InitOnceExecuteOnce`-backed in every STL known here, but a
  newer toolset's imports cannot be proven on this machine, and nothing in the tree needs any of
  them. What to use instead: `std::thread` plus `join` (`_beginthreadex`/`WaitForSingleObjectEx`),
  the SRWLOCK-backed `std::mutex` if a lock is ever needed, plain atomics without
  `wait`/`notify`, and composition-root ownership instead of statics.
- The sRGB output tables (`colour::SrgbOutputTables`, `src/core/colour/Pipeline.cpp`) are the
  case in point: pure math (the decision thresholds of the sRGB OETF and their bucket index) that
  depends on no host call, file, option or `Cicp`, so it is built once per plugin instance - by
  the composition root, in `pvdInit`, ≈ 5 ms on x64 and ≈ 15 ms on x86 in Release (12 / 25 ms in
  Debug) - and borrowed by `const&` through `CodecPlugin` and `FileSession` by every
  `Presentation` of that plugin (`Presentation::outputTables()` and `FileSession::outputTables()`
  expose the borrowed object so a test can pin that every session of one plugin shares one
  instance and that a second plugin has its own). Building it at construction rather than lazily
  on the first HDR session is what keeps it free of synchronisation: `pvdFileOpen` may run on
  several threads at once (the 8-thread leak scenario and the four-thread e2e case do exactly
  that), so a lazily built member would need a lock, and a function-local static would need the
  runtime's guard - the import this section forbids. The price is paid once per `pvdInit`, which
  the host calls once per plugin load; only the leak scenarios that cycle `pvdInit` (init/exit
  cycle, the disk and memory round trips, LoadLibrary/FreeLibrary) see it, ≈ +7 s per plugin in
  Debug x64. The tables are immutable after construction and live in the composition root, which
  outlives every session by construction (§2).
- A `DecodedPage` view is valid until `freePage` or session destruction, whichever comes first.
- In memory mode the file bytes belong to the host and are valid until `pvdFileClose` (SDK
  guarantee); the session stores only a `std::span` and no `IFileData`.
- Known limitation: the file is mapped with `FILE_SHARE_WRITE | FILE_SHARE_DELETE` (so Far can keep
  working with the file while it is shown). If another process truncates the file while a page is
  being decoded, reading the mapped view raises `EXCEPTION_IN_PAGE_ERROR`, a structured exception
  that the C++ `catch (...)` firewall does not see. This is the same behaviour as the bundled
  decoders that map files (e.g. BMP.pvd) and is accepted for v1.

## 8. Out of scope (document, do not implement)

CICP-described colour is converted for display by `src/core/colour`. ICC profile presence is
detected (`ImageMeta::hasIcc`, for the info line) but the bytes are neither retained, applied nor
forwarded through PVD; the host facts behind that decision are in
[`docs/host/pictureview-abi.md`](host/pictureview-abi.md). The PVD
interface provides no user-adjustable exposure or tone controls, and gain maps are not supported.
Also out of scope are
per-plugin configuration files and a plugin that serves several formats from one DLL (one format
per plugin keeps the identity, the priority and the packaging one-dimensional). Format-specific
exclusions live in the plugin's `DESIGN.md`.
