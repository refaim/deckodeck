# Task 4 (core) — fix round after review

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first (§3.7 was
amended: `apply(const Transforms&, PixelView, std::uint64_t maxPixels)` with an identity copy when no
transform is present, and `PixelBuffer::create(..., maxPixels)` — both now canonical). You own only
`src/core/**` and `tests/core/**`. Do not touch anything else. No commits; `git add` at the end.

Build isolation: set `AVIFPVD_BUILD_SUFFIX=-fix4` before any cmake/ctest call.

The reviewer (Opus) rejected Task 4 with three substantive findings and nine nits. Fix all three
findings and nits 1, 3, 4, 5, 6, 7, 8, 9 (nit 2 is resolved by the contract amendment). TDD: for
findings 2 and 3 write the failing tests first and show the red run.

## Substantive findings (verbatim from the review)

1. `src/core/PixelBuffer.cpp:10` — `Error tooLarge(const char *detail)` takes a raw pointer as a
   parameter of one of our own functions. AGENTS rule 3: raw pointers never as a parameter or return
   type of our own functions; `std::string_view` is the mandated type for input strings. Fix:
   `Error tooLarge(std::string_view detail)` returning `Error{ErrorCode::TooLarge, std::string{detail}}`.
   Then grep all of `src/core` for any other raw-pointer parameter/return/member (`\*\s*\w+\s*[,)]`,
   `\w+\s*\*\s*\w+;`) and fix every hit the same way.

2. `src/core/Transform.cpp:162` and `:197` — `apply()` declares `std::optional<PixelBuffer> output;`
   and ends with `return std::move(output.value());`. With `Transforms{}` no branch engages `output`
   and `.value()` throws `std::bad_optional_access` out of a function that promises
   `Result<PixelBuffer>`. Fix: make the no-transform case an identity copy (route it through the crop
   path with the full-image rect, keeping the `maxPixels` check) so `apply` always yields a buffer;
   remove the `std::optional` dance if it is no longer needed; add tests for
   `apply(Transforms{}, view, max)` (pixels identical, dimensions identical) and for the identity case
   exceeding `maxPixels` → `TooLarge`. `FileSession` may keep calling `hasTransforms` to skip the copy,
   but `apply` itself must have no precondition.

3. `tests/core/FakeTests.cpp` — asserts only that the fakes throw when their `throwOn*` flags are set;
   exercises no `src/core` code. The property the scaffolding exists for — AGENTS rule 5, exceptions
   only propagate, no catch in core — is untested. Fix: replace `FakeTests.cpp` with tests that drive
   the real core entry points through the throwing fakes: `AvifPlugin::open` with a throwing
   `looksLikeAvif`, a throwing `IFileSource::open`, a throwing `IDecoderFactory::create`;
   `FileSession::pageInfo` with a throwing `frameTiming`; `FileSession::decodePage` with a throwing
   `decodeFrame` and with a throwing `Progress` callback. Use `CHECK_THROWS_AS` with the exact
   exception type, and assert cleanup afterwards via the existing `dataDestructions` / `destructions`
   counters (no `IFileData`, decoder or pixel buffer leaks; `outstanding_` unchanged). Delete any
   `throwOn*` flag that ends up unused.

## Nits to address

1. `src/core/Transform.cpp` — at the index maps for rotation (`:35`, `:49-65`) and mirror (`:91-94`)
   add comments citing the installed `avif.h` lines: "angle * 90 specifies the angle (in
   anti-clockwise direction)" and "axis 0: the top and bottom parts of the image are exchanged",
   plus the HEIF clause numbers, the way the clap→irot→imir comment already does.
3. `src/core/PixelBuffer.cpp:31-34` — move the `pitch > UINT32_MAX` check up next to the overflow
   check so `size` is not computed for a request that will be rejected.
4. `src/core/Transform.cpp:115` — one comment line explaining that the bitwise `&` chain is
   deliberate (one branch region instead of four) so nobody "fixes" it to `&&`.
5. `src/core/Describe.cpp` — include `<cstddef>` explicitly; replace `names.at(...)` with an indexed
   access (no legal `ChromaFormat` value can be out of range; `.at` introduces an unreachable throw).
6. `tests/core/FileSessionTests.cpp:201` — do not pass a span whose `data()` was freed; assert
   `freePage` on a freshly decoded page's address instead.
7. `tests/core/FileSessionTests.cpp:47` — remove the tautological `&session.imageInfo() ==
   &session.imageInfo()`; assert the actual field values instead.
8. `tests/core/FileSessionTests.cpp:251-269` — after decoding the second page, re-read the first
   page's pixel content and assert it is unchanged (this is the assertion that pins "the view stays
   valid until freePage").
9. `tests/core/TransformTests.cpp:79-81` — hoist `pixelIds(actual.view())` out of the innermost loop.

## Verify and report
`cmake --preset debug && cmake --build --preset debug --target core_tests guard_tests && ctest --preset debug -R "core|guard"`,
then `scripts/coverage.ps1` (the whole-repo gate must pass now — quote the src/core rows and TOTAL;
if another directory fails the gate, quote it and say so). Include the red runs for findings 2 and 3,
the final doctest counts, and a grep proving no raw-pointer parameters/returns/members remain in
`src/core`. List anything you did not do.
