# Task 4 — core logic (`src/core`, `tests/core`)

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first; §3.6,
§3.7 and §5 (`tests/core`) are your part. Infrastructure and canonical headers (`src/core/Error.hpp`,
`src/core/IFileSource.hpp`, `src/core/IDecoder.hpp`, `src/pvd/Types.hpp`, `src/pvd/Plugin.hpp`) already
exist — do not restructure them. Other agents are working **at the same time** in `src/pvd`,
`src/adapters`, `tests/pvd`, `tests/adapters`. You own only `src/core/**` and `tests/core/**`. Do not
edit anything else; if a shared file must change, describe it in your final report.

`src/core` includes no foreign headers, ever. Everything you need comes through the interfaces; tests
use fakes (`FakeFileSource`, `FakeFileData`, `FakeDecoderFactory`, `FakeDecoder`) that record calls and
can be scripted to return any `ErrorCode`, throw, or produce synthetic pixels.

Build isolation: set `AVIFPVD_BUILD_SUFFIX=-t4` before any cmake/ctest call.

## Deliverables (TDD: failing test first, every time)

1. `src/core/PixelBuffer.hpp/.cpp` — per §3.7. Static factory `Result<PixelBuffer> create(width,
   height, bytesPerPixel, maxPixels)` returning `TooLarge` when `width * height > maxPixels` or when
   `pitch * height` overflows; `make_unique_for_overwrite`. `PixelView view() const`.
2. `src/core/Transform.hpp/.cpp` — per §3.7: `validatedCrop` (rect inside image, non-empty, else
   `InvalidTransform`), `crop`, `rotate` (angles 0–3, anti-clockwise; 0 is a copy), `mirror`
   (`TopBottom`, `LeftRight`), `displaySize(meta)` (crop first, then swap for odd angles),
   `apply(transforms, view, maxPixels)` in the order clap → irot → imir, returning the input copied
   when no transform is present is NOT required — the session only calls `apply` when
   `hasTransforms(transforms)` is true; provide that predicate. Work for bytesPerPixel 3 and 4.
   Tests: 2×3 and 3×2 hand-derived matrices for every angle and axis, crop corners, combined
   clap+irot+imir, invalid crop (outside, zero size), sizes after each op.
3. `src/core/Describe.hpp/.cpp` — `describe(const ImageMeta&)` exactly per §3.7 format. Tests: every
   chroma, both ranges, every listed CICP triple plus an unknown one, alpha straight/premultiplied/none,
   still vs animated (1 vs N frames), each metadata flag, each transform, and the full combined string.
4. `src/core/FileSession.hpp/.cpp` — per §3.7. Constructor takes `std::unique_ptr<IFileData>` (may be
   null in memory mode), `std::unique_ptr<IDecoder>`, `ImageInfo`, `DecoderOptions`. Behaviour exactly
   as specified: range checks, `bitsPerPixel = depth × channels`, `frameTimeMs` only when animated,
   `Progress::report(0,3)/(1,3)/(2,3)` with `Aborted` on `false` at each step, transforms applied only
   when present, outstanding buffers, `freePage` by data pointer (true/false).
   Tests through fakes: memory mode and file mode; every error path of the decoder; abort at each of
   the three steps (assert no buffer is retained after an abort); alpha → `Bgra32`/32 bpp, no alpha →
   `Bgr24`/24 bpp; 10-bit reports 30/40 bpp; animated timing passthrough and timing error propagation;
   transforms change reported size and pixels; two outstanding pages then free in either order; free of
   unknown pointer; destruction with outstanding pages (use a counting fake to prove buffers die).
5. `src/core/AvifPlugin.hpp/.cpp` — per §3.7. Constructor `(IFileSource&, IDecoderFactory&,
   DecoderOptions, pvd::PluginInfo)`. `info()` returns the stored `PluginInfo`. `open()`: signature
   check → `NotAvif`; memory mode uses `head` as the whole file and no `IFileData`; file mode opens
   via the source (error passthrough) and hands the data to the session; factory error passthrough;
   `ImageInfo{frameCount, animated, "AVIF", "AV1", describe(meta)}`.
   Tests: each branch, including that in memory mode the file source is never called and in file mode
   the head is not used for decoding.
6. `src/core/CMakeLists.txt`: `avifpvd_core` STATIC.

## Verify and report
`cmake --preset debug && cmake --build --preset debug --target core_tests guard_tests && ctest --preset debug -R "core|guard"`,
then `scripts/coverage.ps1` — your gate is `src/core/**` at 100 % lines and 100 % branches (quote the
per-file rows; other directories may be incomplete because other agents are still working). Include
exact commands and outputs. `git add` your files; do not commit.
