You are the implementer of Task 20 in the `pvdkit` repository (current directory
C:\Users\Roma\Dev\PictureView3\pvdkit — Windows 10, clang-cl 19 + lld-link, C++23, CMake presets,
vcpkg static triplets, doctest). The tree is clean at commit 2c5e42b. Read `AGENTS.md` (strict C++23,
no new/delete, unique_ptr ownership, TDD, 100 % line+branch coverage of `src/**` and
`plugins/*/src/**`, zero warnings, never commit, no worktrees), `docs/ARCHITECTURE.md` §3 (colour
pipeline placement), `src/core/colour/*` (Transfer, Primaries, ToneMap, Pipeline — the HDR
presentation), `src/core/FileSession.cpp` (row-by-row `Presentation::apply` after `decodeFrame`),
`src/core/IDecoder.hpp` (`DecoderOptions::maxThreads`), `docs/tasks/task16-colour.md` and
`docs/tasks/report-task16.md` (design, anchors, the ffmpeg comparison).

## Problem — measured in Far by Roma
`cosmos1650_yuv444_10bpc_p3pq.avif` (1024×428, P3/PQ): decode time went from 10 ms (1.1.0, no
presentation) to 91 ms (1.2.0). That is ~180 ns per pixel for the presentation pass, dominated by
`std::pow`/`exp`/`log` in the PQ EOTF/OETF used by the BT.2390 EETF (10 transcendental calls in
`src/core/colour/*.cpp`) — a 12-megapixel HDR photo would take ~2 s. Target: the presentation pass
must cost ≤ 10 ms on this fixture on the release build (≥ 8× faster), **with results bit-identical
to the current output** (the pinned BGRA16 samples in the AVIF e2e/adapter tests and the ffmpeg
comparison must not change; add a whole-image hash test of the current output for the two HDR
fixtures BEFORE optimising, so identity is enforced, not hoped for — if an optimisation changes any
16-bit value by ±1 due to LUT interpolation, that is NOT acceptable in this task: keep exact math
where it decides the value, tabulate only what is exactly tabulable).

## Approach (in this order; measure after each step; stop when the target is met)
1. **Measure first.** A release-build timing harness (doctest case in `tests/core/colour`, no gate,
   `MESSAGE` output) that runs `Presentation::apply` over a 1024×428 buffer 20× and prints the
   median ns/pixel, plus the same for a synthetic 4000×3000 buffer. Record the baseline in the report.
2. **Transfer LUTs everywhere they are exact.** The input transfer (PQ/HLG EOTF) is already a
   65,536-entry LUT on the 16-bit input; verify the *output* sRGB OETF is not computed per pixel
   with `pow` — if it is, it cannot be a plain LUT (input is float), but a 4096-entry LUT with
   linear interpolation is NOT allowed here (changes bits). Instead: keep the exact OETF but make
   it cheap — `std::pow(x, 1/2.4)` is the cost; compare against an exact-enough polynomial/`exp2`
   formulation only if bit-identical after quantisation on the full 16-bit output grid (test that
   by exhaustive comparison over the LUT domain where possible). If exactness cannot be proven,
   leave the OETF as is and take the win elsewhere.
3. **Tone map without per-pixel `pow`.** The EETF acts on one scalar (maxRGB in linear light) and
   returns a scale factor. Its PQ transforms are monotonic; build the EETF as a function of the
   PQ-domain value and note that the PQ *input* value of a pixel is already available before the
   EOTF LUT (the 16-bit code) — but maxRGB is taken after the primaries matrix, so the input code
   is not the argument. Options: (a) compute maxRGB → EETF via an exact per-pixel path but with the
   PQ OETF/EOTF pair replaced by a single precomputed monotone mapping over a fine grid *only if*
   bit-identical (unlikely); (b) reorder: BT.2390 is applied in the PQ domain of luminance-like
   maxRGB — if the pipeline can apply the primaries matrix in PQ-encoded space? no, it cannot (the
   matrix is linear). So the honest answer is likely: keep the exact EETF but reduce its cost —
   `pow` → `exp2(log2)` fusions, `float` not `double`, avoid recomputing constants per pixel,
   hoist everything invariant out of the row loop, make the per-pixel body branch-free so clang
   auto-vectorises (`/O2`, check the vectorisation remarks with `-Rpass=loop-vectorize` on that TU
   and quote them). Report exactly which of these were possible while staying bit-identical.
4. **Threads.** `FileSession::decodePage` splits the rows into `min(maxThreads, 4)` bands and runs
   `Presentation::apply` per band with `std::jthread`s (or a small band-parallel helper in
   `src/core/colour/Pipeline.cpp`); single-threaded when the image is smaller than 256 K pixels.
   `Presentation` is immutable after construction and `apply` is `const noexcept`, so sharing is
   safe — assert that in a comment and a test (two threads over disjoint bands produce the same
   bytes as one thread). Exceptions cannot cross thread boundaries: `apply` is noexcept, keep it so.
   The AVIF composition already passes `hardware_concurrency()` as `maxThreads`.
5. If after 1–4 the target is still missed, say so with numbers and stop — do not trade exactness
   for speed; propose the LUT-with-interpolation variant as a follow-up with its measured max error.

## Deliverables
Tests (TDD, identity hashes first), coverage 100/100 both archs, lint clean both archs, `ctest` for
`debug`, `release`, `debug-x86`, `release-x86`, `asan`; the timing table (before/after, per step,
ns/pixel and ms for the cosmos fixture and the 12-Mpx synthetic, x64 release; x86 release for the
cosmos fixture); vectorisation remarks; ChangeLog: add to the existing `AVIF 1.2.0` entry a plain
Russian line only if user-visible (it is — "HDR-файлы открываются быстрее" is not needed as a
separate line; fold into the HDR line or skip — your call, say which). Version stays 1.2.0.
Environment: `$env:PVDKIT_BUILD_SUFFIX = "-t20"`; `--parallel 6`, one build/lint at a time; `pwsh`
not on PATH (`powershell -NoProfile -ExecutionPolicy Bypass -File`); no network; never touch
`C:\Tools\FarManager`; never commit. Report `docs/tasks/report-task20.md`, printed as your final
message, with the four Release DLL hashes.
