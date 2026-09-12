# pvdkit — architecture

Status: v2 design, 2026-09-12 (v1 was the single-plugin AVIF.pvd design of 2026-09-09; Task 7
turned it into this monorepo). Owner: orchestrator. Implementers: Codex/Claude agents. Reviewers:
Opus agents. Rules that constrain this design live in `AGENTS.md`. Interface declarations below
are canonical: implement them as written; if something is genuinely impossible, report instead
of improvising. Plugin-specific design (what AVIF.pvd does with libavif, its fixtures, its
limitations) lives next to the plugin: `plugins/avif/DESIGN.md`.

## 0. Shape of the repository

```
pvdkit/
  src/pvd/            shared: the PVD boundary (Types, Plugin, Progress, Shim, ContextHandle,
                      Firewall, PvdApi, PluginFactory, PluginIdentity, Exports.cpp, Plugin.def,
                      Plugin.rc, PluginConstants.hpp.in)                      → pvdkit_pvd
  src/core/           shared: Error, Narrow, PixelBuffer, Transform, IDecoder, IFileSource,
                      IImageDescriber, CodecPlugin, FileSession                → pvdkit_core
  src/adapters/win/   shared: FileMapping, FileSource, Utf8                    → pvdkit_win
  plugins/<id>/       one directory per plugin (today: avif → AVIF.pvd)
    CMakeLists.txt    identity (name, version, priority), libraries, composition, packaging
    src/core/         plugin decisions without the codec library (e.g. the describer)  → <id>_core
    src/adapters/     the codec adapter(s)                                     → <id>_adapter
    src/DefaultPlugin.cpp   the composition root: pvd::makePlugin()            → <id>_composition
    tests/core/ tests/adapters/ tests/e2e/   the plugin's tests
    fixtures/  scripts/  package/README.txt.in  README.md  DESIGN.md
  tests/pvd/ tests/core/ tests/adapters/ tests/guard/   shared tests
  tests/e2e/          the shared host driver + VERSIONINFO test, compiled into every plugin's e2e
  cmake/pvdkit-plugin.cmake   pvdkit_plugin_identity / pvdkit_add_plugin / pvdkit_add_plugin_e2e_tests
  scripts/            coverage, check-imports, check-exports, build-all, package (all plugins)
  ports/ triplets/ vcpkg.json CMakePresets.json   one toolchain and one manifest for the kit
  docs/               this file and the task history
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
- `pvdPageDecode(ctx, iPage, pDecodeInfo, callback, cbCtx)`: fill `pImage` (BGR 24 or BGRA 32,
  8 bits per channel), `nBPP`, `lImagePitch` (positive = top-down rows), `pPalette = nullptr`,
  `nColorsUsed = 0`, `Flags` (we never set `PVD_IDF_READONLY`: the buffer is ours and writable).
  `callback` may be `NULL`; if it returns `FALSE` we stop and return `FALSE`.
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
`IDecoderFactory&`, `const IImageDescriber&`) outlive their users by construction (the composition
root owns them in declaration order). Classes holding references delete copy and move.

## 3. Canonical interfaces

Namespace for everything shared: `pvdkit`, with sub-namespaces `pvd`, `core`, `win`. A plugin
uses a sub-namespace named after itself for all of its code (`pvdkit::avif`), including the
describer it places under `src/core/`.

### 3.1 `src/core/Error.hpp`

```cpp
enum class ErrorCode {
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

### 3.4 `src/pvd/Shim.hpp`, `ContextHandle.hpp`, `Firewall.hpp`, `PluginIdentity.hpp`

- `Firewall`: `template <class F> auto guarded(F&& f, decltype(f()) fallback) noexcept` — runs `f`,
  returns `fallback` on any exception. A `void` overload swallows. This is the only `catch (...)` in
  the codebase. Tested with a fake that throws `std::bad_alloc`, `std::runtime_error`, and an `int`.
- `ContextHandle`: `void* toHost(std::unique_ptr<IFileSession>)`,
  `std::unique_ptr<IFileSession> fromHost(void*)`, `IFileSession* borrow(void*)` (the pointer stays on
  the adapter line: callers immediately dereference or check null).
- `PluginIdentity` (`src/pvd/PluginIdentity.hpp`): `struct { std::uint32_t priority;
  std::string_view name, version; }` — the constant identity of one plugin, literal-backed (the
  host receives `data()` as C strings). The shared layer only forwards a value it is given; each
  plugin's value `kPluginIdentity` comes from its generated `pvd/PluginConstants.hpp` (§3.4a).
- `Shim` (constructed over `IPlugin&` and a `PluginIdentity`, non-copyable):
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
  `pvdInfoDecode` from `DecodedPage`. `pageFree` calls `freePage(span over pImage)` — the span length
  is unknown to the host, so the session matches by `data()` only.
- `Exports.cpp`: the shared composition root, compiled once into every plugin DLL (and into
  `pvd_tests`). `pvdInit` creates `std::unique_ptr<IPlugin>` via `pvd::makePlugin()` (declared in
  `src/pvd/PluginFactory.hpp`, **defined** in the plugin's `DefaultPlugin.cpp`, or by the test) and
  a `Shim` over it and `kPluginIdentity`; `pvdExit` resets. If an export is called without a live
  shim: `pvdPluginInfo` fills the identity with empty comments; the others return `FALSE`/no-op.
  Everything is a one-line forward wrapped in `guarded`. It is the only shared file that may
  include the generated `pvd/PluginConstants.hpp` (guard rule).
- `PvdApi.hpp` includes `<Windows.h>` (lean, `NOMINMAX`) and the SDK header inside `extern "C"`
  exactly once, for `Shim`, `Exports.cpp` and the tests.
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
pvdkit_plugin_identity(avif NAME AVIF VERSION 1.0.0 PRIORITY 10
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
- the package manifest `<build>/plugins/<id>/package/manifest.json` and README that
  `pvdkit_add_plugin` writes for `scripts/package.ps1`.

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
enum class ChromaFormat { Yuv444, Yuv422, Yuv420, Yuv400 };
struct Cicp { std::uint16_t primaries, transfer, matrix; bool fullRange; };
struct CropRect { std::uint32_t x, y, width, height; };
enum class MirrorAxis { TopBottom, LeftRight };   // named after the effect, never a magic number
struct Transforms {                               // every field at its default = "none"
  std::optional<CropRect> clap;       // already converted from clap fractions to a pixel rect by the adapter
  std::uint8_t irotAngle = 0;         // 0..3 quarter turns, anti-clockwise (HEIF 'irot')
  std::optional<MirrorAxis> imir;
};
struct ImageMeta {
  std::uint32_t width, height;        // coded size, before transforms
  std::uint8_t depth;                 // bits per sample of the source (8, 10, 12, ...)
  ChromaFormat chroma;                // sample layout in CICP terms (see below)
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

Decisions (Task 7, open point 1):

- `Transforms` stays a shared concept and `Transform` stays in `src/core`: `Transforms{}` is a
  valid "none", HEIF-family formats all carry clap/irot/imir, and a format without them simply
  leaves the struct empty so `FileSession` never calls `Transform::apply`. No transform step is
  injected.
- `chroma` and `cicp` stay plain (non-optional) fields. They are the ISO/IEC 23091-2 signalling of
  the coded samples, and CICP itself has a spelling for every case: an RGB format reports
  `Yuv444` (no subsampling) with `cicp.matrix = 0` (identity) and `fullRange = true` — for sRGB
  PNG that is exactly `{1, 13, 0, true}`, for unknown colour `{2, 2, 0, true}` — and greyscale is
  `Yuv400`. Nothing in the shared core reads them; only a plugin's describer does, and it is free
  to print them or not. An `std::optional` would have added branches to every describer for a
  case CICP already expresses.
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
- `CodecPlugin : pvd::IPlugin` (`src/core/CodecPlugin.hpp/.cpp`), ctor
  `(IFileSource&, IDecoderFactory&, const IImageDescriber&, DecoderOptions, PluginInfo)`.
  `open()`: `recognises(head)` else `NotRecognised`; if `fileSize == 0` data = `head`, else
  `fileSource.open(name)` → `IFileData` owned by the session; `factory.create(data, options)`;
  `describer.describe(meta)`; build `ImageInfo{frameCount, animated, formatName, compression,
  comments}`; return `FileSession`. It is the same class for every plugin.
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
- A plugin's composition root (`plugins/<id>/src/DefaultPlugin.cpp`) defines `pvd::makePlugin()`
  returning an object that owns `win::FileSource`, the plugin's `IDecoderFactory`, its
  `IImageDescriber` and a `core::CodecPlugin` (declared in that order) and forwards `IPlugin`.
  It chooses the `DecoderOptions` and builds `PluginInfo` from `kPluginIdentity` plus run-time
  library versions. AVIF's values: `plugins/avif/DESIGN.md`.

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

- CMake ≥ 3.28, presets in `CMakePresets.json`: `debug`, `release`, `coverage` (x64) and
  `debug-x86`, `release-x86`, `coverage-x86` (32-bit; all Ninja, clang-cl, lld-link). The names
  are unchanged from v1; every preset builds every plugin. `-DPVDKIT_PLUGINS=<id>[;<id>]`
  restricts the build (and the vcpkg manifest features, see below) to those plugins. The x86
  presets differ from the x64 ones only in `VCPKG_TARGET_TRIPLET = x86-windows-static-clang`;
  `cmake/vcpkg-root.cmake` reads `triplets/<triplet>.cmake` to pick the chainload toolchain
  (`cmake/clang-cl-x86.toolchain.cmake`: the same x64-hosted clang-cl cross-compiling with
  `CMAKE_<LANG>_COMPILER_TARGET = i686-pc-windows-msvc`), so the triplet file is the one place
  that maps an architecture to its toolchain. The x86 triplet also sets `VCPKG_LOAD_VCVARS_ENV ON`:
  vcpkg does not load vcvars for chainloaded triplets, and without it meson (dav1d) activates
  `vcvars64.bat` itself and links i686 objects against the x64 CRT (`_mainCRTStartup` unresolved).
  A plugin lands in `build/<preset><suffix>/plugins/<id>/<NAME>.pvd` (x86 builds are
  `IMAGE_FILE_MACHINE_I386`).
  `release`: `/O2`, `/MT`, `/DEBUG:NONE`. `coverage`: `/Od /Zi /MT`
  + `-fprofile-instr-generate -fcoverage-mapping` on every target.
- Common flags: `/clang:-std=c++23 /W4 /WX /permissive- /utf-8 /EHsc /Zc:preprocessor`
  plus `-Wno-` nothing unless justified. `static_assert(__cplusplus >= 202302L)` in `src/core/Error.hpp`.
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
  - `pvdkit_add_plugin(<id> LINK <id>_composition README package/README.txt.in LICENSES <port> <name> ...)`
    → `<id>_plugin` (SHARED; `OUTPUT_NAME <NAME>`, `SUFFIX .pvd`, `PREFIX ""`; sources =
    `Exports.cpp` + `Plugin.def` + `Plugin.rc` read from properties of `pvdkit_pvd`) and the
    package staging `<bindir>/package/` (`README.txt` configured from the plugin's template with
    name/version/priority/description/comments and architecture/author/copyright, `LICENSES.txt`
    assembled from vcpkg's `share/<port>/copyright` files, `manifest.json` = name, version,
    architecture, file name);
  - tests: `<id>_core_tests`, `<id>_adapter_tests` (the plugin's own `add_executable`), and
    `pvdkit_add_plugin_e2e_tests(<id> FIXTURES <dir> SOURCES ...)` → `<id>_e2e_tests` (in
    coverage builds with the ctest `ENVIRONMENT` property
    `LLVM_PROFILE_FILE=<build>/pvdkit-<id>-%p-%m.profraw`, so the coverage gate can tell this
    DLL's profile from every other plugin's) plus the ctest entries `<id>_check_imports` and
    `<id>_check_exports` (Release configuration).
  Shared tests: `pvd_tests` (compiles `Exports.cpp` against `pvd_tests_identity`), `core_tests`,
  `adapter_tests` (win), `guard_tests`. Per-directory `CMakeLists.txt` with
  `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`; the top level globs `plugins/*/CMakeLists.txt` the
  same way, so a new plugin is a new directory and nothing else.
- `scripts/check-imports.ps1`: fails unless the import table of the given DLL is exactly `KERNEL32.dll`
  (`llvm-readobj --coff-imports`). `scripts/check-exports.ps1`: exactly the eight bare `pvd*` names.
  Both are registered per plugin as ctest tests in the release presets.
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
  and `dist/x86/<NAME>.pvd`. `scripts/package.ps1`: the same from scratch (suffix `-pkg`), checks
  each DLL's `FileVersion` against the manifest, then `dist/<NAME>-<version>-{x64,x86}.zip`
  (`<NAME>.pvd`, `README.txt`, `LICENSES.txt` from the staging directory) with their SHA-256.

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
- `tests/guard`: walks `src/` and every `plugins/*/src/` and fails on forbidden tokens: `new `,
  `new(`, `delete `, `malloc`, `calloc`, `realloc`, `free(`, `shared_ptr`, `weak_ptr`,
  `reinterpret_cast` outside adapters/pvd, `#include <windows.h>` outside `src/adapters/**`,
  `plugins/*/src/adapters/**`, `src/pvd/Exports.cpp` and `src/pvd/PvdApi.hpp` (the same set
  AGENTS.md rule 4 names), codec headers (`avif/avif.h`, `dav1d/dav1d.h`; extend the list with
  each plugin's library) outside those adapters and `Exports.cpp`, `catch (`
  outside `Firewall.hpp`, `LCOV_EXCL`, `__builtin_unreachable`, `[[assume`. Layering rules,
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
   src/DefaultPlugin.cpp)`, `pvdkit_add_plugin(<id> LINK <id>_composition README package/README.txt.in
   LICENSES <port> "<name (licence)>" ...)`, `add_subdirectory(tests)` under `BUILD_TESTING`.
2. `vcpkg.json`: a feature `<id>` with the codec ports; add it to `default-features`.
3. `src/adapters/<lib>/`: `IDecoderFactory` (`recognises` = signature check on the head,
   `create` = parse) and `IDecoder` over the library, one-to-one, no decisions.
   `src/core/`: the `IImageDescriber` and any other decision that needs no library.
   `src/DefaultPlugin.cpp`: `pvd::makePlugin()` owning `win::FileSource`, the factory, the
   describer and a `core::CodecPlugin`.
4. `fixtures/` + `fixtures/SOURCES.md`, `tests/core`, `tests/adapters`, `tests/e2e/E2eTests.cpp` +
   `pvdkit_add_plugin_e2e_tests(<id> FIXTURES ... SOURCES ...)`, `package/README.txt.in`,
   `README.md`, `DESIGN.md`.
5. Extend the guard's codec-header list with the new library's header. Nothing under `src/`,
   `scripts/` or `CMakePresets.json` changes.

## 7. Concurrency and lifetime

- The host may decode several files at once (prefetch). Every session is independent; the only
  process-wide state is the composition root created in `pvdInit`. Codec libraries must be
  thread-safe across decoder instances (libavif/dav1d are).
- A `DecodedPage` view is valid until `freePage` or session destruction, whichever comes first.
- In memory mode the file bytes belong to the host and are valid until `pvdFileClose` (SDK
  guarantee); the session stores only a `std::span` and no `IFileData`.
- Known limitation: the file is mapped with `FILE_SHARE_WRITE | FILE_SHARE_DELETE` (so Far can keep
  working with the file while it is shown). If another process truncates the file while a page is
  being decoded, reading the mapped view raises `EXCEPTION_IN_PAGE_ERROR`, a structured exception
  that the C++ `catch (...)` firewall does not see. This is the same behaviour as the bundled
  decoders that map files (e.g. BMP.pvd) and is accepted for v1.

## 8. Out of scope (document, do not implement)

Colour management of any kind (the PVD interface has none), 16-bit output, per-plugin
configuration files, a plugin that serves several formats from one DLL (one format per plugin
keeps the identity, the priority and the packaging one-dimensional). Format-specific exclusions:
the plugin's `DESIGN.md`.
