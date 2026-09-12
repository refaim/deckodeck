# Task 8 — RPGMVP.pvd (RPG Maker MV/MZ encrypted PNG) in the pvdkit monorepo

Goal: a second plugin, `plugins/rpgmvp` → `RPGMVP.pvd`, x64 and x86, same guarantees as AVIF.pvd
(static, KERNEL32-only, eight bare exports, 100 % line+branch coverage, VERSIONINFO, zip).
Reference material (read it): `C:\Users\Roma\Dev\PictureView3\ref\rpgmvp\rpgmvp\spec.md` (older
spec, mostly right; where this task disagrees, this task wins), the JS decrypter under
`ref\rpgmvp\rpgmvp-ref\Decryptors\RPG-Maker-MV-Decrypter\scripts\Decrypter.js`, and the png/rpgmvp
fixture pairs under `ref\rpgmvp\rpgmvp\test\fixtures\`.

## Format (verified by the orchestrator on 1802 real files from three games)

```
0x00  16  fake header: 52 50 47 4D 56 00 00 00 | 00 03 01 | 00 00 00 00 00   ("RPGMV", version 0.3.1)
0x10  16  original PNG bytes 0..15 XOR key (key = MD5 of the game's encryptionKey; irrelevant here)
0x20  ..  original PNG bytes 16..end, verbatim
```
PNG bytes 0..15 are constant for every PNG: `89 50 4E 47 0D 0A 1A 0A 00 00 00 0D 49 48 44 52`
(signature, IHDR length 13, "IHDR"). Therefore: skip 32 bytes, prepend that constant — no key
needed. Extensions in the wild: `.rpgmvp` (MV) and `.png_` (MZ); identical layout. Same scheme
exists for audio (`.rpgmvo`/`.ogg_`) — out of scope.

## Detection (pvdFileOpen)
1. `head.size() >= 49` (32 + 17 bytes: IHDR data 13 + CRC 4), first 8 bytes == `RPGMV\0\0\0`.
2. After substitution, the IHDR CRC (over "IHDR" + 13 data bytes, i.e. reconstructed bytes 12..28,
   CRC at 29..32) must match — this rejects garbage that merely starts with RPGMV and makes the
   version bytes (`00 03 01`) informational, not a gate (accept other versions if the CRC holds).
3. Plain PNG (`89 50 4E 47`) → `NotRecognised` (the format-neutral ErrorCode; the `NotAvif`
   rename is done, Task 7).
4. Files inside archives (memory mode) work the same: the head is the whole file.

## Decoding
- libspng 0.7.4 from vcpkg (static; pulls zlib). Adapter `plugins/rpgmvp/src/adapters/spng/`
  implementing `core::IDecoder`/`IDecoderFactory` over `spng_ctx`: `spng_ctx_new(0)`,
  `spng_set_png_stream` with a read callback that serves the 16-byte constant first and then the
  mapped span from offset 32 — **no copy of the PNG**; `spng_get_ihdr`, `spng_decoded_image_size`,
  `spng_decode_image(ctx, out, len, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS)` (handles palette, tRNS,
  16→8 bit, Adam7, gamma NOT applied — document), then swap R↔B in place into the caller's
  BGRA buffer (or use `SPNG_FMT_BGRA8` if libspng 0.7.4 offers it — check `spng.h`; prefer the
  native format). Chunk limits: `spng_set_image_limits(ctx, maxDimension, maxDimension)` and
  `spng_set_chunk_limits` for hostile files. Errors via `spng_strerror` in `Error::detail`.
- `ImageMeta`: width, height, depth (bit depth 1/2/4/8/16), `hasAlpha` = colour type 4/6 or tRNS
  present (query `spng_get_trns`), no transforms, frameCount 1, `animated = false` (APNG is not
  supported: report 1 page even if `acTL` exists — libspng decodes the default image; document).
  `chroma` and `cicp` are plain fields (Task 7 decision, ARCHITECTURE §3.6): `chroma = Yuv444`
  for RGB / RGBA / palette (colour types 2, 3, 6), `Yuv400` for greyscale with or without alpha
  (0, 4); `cicp = {2, 2, 0, true}` (unspecified primaries/transfer, identity matrix), or
  `{1, 13, 0, true}` when the file carries an `sRGB` chunk; `fullRange = true` always.
- Output: **always BGRA32** (spec §4.2 — sprites/tilesets with alpha), pitch = width × 4,
  top-down, straight alpha. `PageInfo::bitsPerPixel` = depth × channels of the source (e.g. 32 for
  RGBA8, 64 for RGBA16, 8 for indexed8, 4 for indexed4) — informational, like AVIF.
- `ImageInfo`: produced by the plugin's describer, a `core::IImageDescriber` in
  `plugins/rpgmvp/src/core/` (namespace `pvdkit::rpgmvp`, no libspng include) returning
  `ImageDescription{"RPGMVP", "Deflate", comments}`: `formatName` is "RPGMVP" always (we cannot
  see the extension), `compression` "Deflate", `comments` like
  `"RPG Maker MV/MZ encrypted PNG, 8-bit RGBA, interlaced, tRNS"` (colour type name, depth,
  `interlaced` if Adam7, `palette` if indexed, `tRNS` if present, `PNG header version 0.3.1`).
  `CodecPlugin` adds `pageCount` / `animated` from the meta.
- Plugin identity: declared once in `plugins/rpgmvp/CMakeLists.txt` as
  `pvdkit_plugin_identity(rpgmvp NAME RPGMVP VERSION 1.0.0 PRIORITY 10 DESCRIPTION "RPG Maker
  MV/MZ encrypted PNG decoder plugin for PictureView (Far Manager)" COMMENTS "RPG Maker MV/MZ
  encrypted PNG decoder: libspng <ver>, zlib <ver>; static build")`, the COMMENTS computed by
  CMake from the installed libspng and zlib versions (as `plugins/avif/CMakeLists.txt` does for
  libavif/dav1d/libyuv) and reproduced at run time in `DefaultPlugin.cpp` from
  `spng_version_string()` / `zlibVersion()`; the shared e2e version test pins the two equal.

## Fixtures (`plugins/rpgmvp/fixtures/`, commit them, SOURCES.md with provenance + expected values)
From Roma's sample games (`C:\Users\Roma\Desktop\rpgmvp`, permission given for small sprites), one
per PNG kind, keep the smallest; verify each with ffprobe after decrypting with the 32-byte rule and
record width/height/colour type/depth/interlace/tRNS and a few exact pixel values (decrypt with a
committed script, `plugins/rpgmvp/scripts/rpgmvp-decrypt.ps1`, pure PowerShell, 32-byte rule):
- `Awakening to Lust - RA26073439\www\img\system\Shadow2.rpgmvp` (RGBA8, 36×16, 400 B)
- `Awakening to Lust - RA26073439\www\img\characters\!$cursor_small.rpgmvp` (indexed 4-bit + tRNS, 144×192)
- `Awakening to Lust - RA26073439\www\img\system\Weapons3.rpgmvp` (indexed 8-bit + tRNS, 288×384)
- `Awakening to Lust - RA26073439\www\img\system\GameOver.rpgmvp` (RGB8, 816×624, 7 KB)
- `Awakening to Lust - RA26073439\www\img\menus\equip\Par.rpgmvp` (RGBA16, 60×20)
- `SpyBreak_Win_1.5\img\system\Shadow2.png_` (indexed8 + tRNS, 82×38, `.png_` variant, different key)
- `SpyBreak_Win_1.5\img\characters\lolded.png_` (RGBA8, 576×384)
- `Story by Story - Demo (PC)\img\pictures\Charakter\Ters\Kamen.png_` (RGBA8 Adam7-interlaced, 700×700, 26 KB)
- from `ref\rpgmvp\rpgmvp\test\fixtures\`: `rgb16_88x4a.rpgmvp` + its `.png` twin (RGB16), and one
  more pair as a "known-good twin" test: decrypting must yield a byte-identical PNG.
- Synthetic (generate with ffmpeg + the inverse 32-byte wrap in the same script): grayscale 8
  (`gray`), gray+alpha (`ya8`), 1-bit, 2-bit palette if ffmpeg can (`-pix_fmt pal8` with few
  colours + pngquant-free approach may not give 1/2-bit — if not possible, note it and cover the
  bit-depth branches with libspng's own behaviour on the 4-bit fixture).
- Negatives: plain `.png` (must be rejected), garbage with `RPGMV` prefix and bad CRC, truncated
  file cut inside IDAT (open OK, decode fails cleanly), 31-byte and 48-byte stubs, a file whose
  IHDR says 100000×100000 (size limit → TooLarge before allocation), an APNG wrapped as rpgmvp
  (decodes first frame, 1 page).

## Steps (ARCHITECTURE §6, the monorepo checklist)
1. `vcpkg.json`: feature `rpgmvp` (`libspng`, which pulls `zlib`), added to `default-features`.
2. `plugins/rpgmvp/CMakeLists.txt`: `find_package(SPNG CONFIG REQUIRED)` (check the name the
   port exports), the comments string, `pvdkit_plugin_identity(rpgmvp ...)` as above,
   `rpgmvp_core` (`src/core/**`: the describer and the detection/CRC logic), `rpgmvp_adapter`
   (`src/adapters/spng/**`), `rpgmvp_composition` (`src/DefaultPlugin.cpp`),
   `pvdkit_add_plugin(rpgmvp LINK rpgmvp_composition README package/README.txt.in LICENSES libspng
   "libspng (BSD-2-Clause)" zlib "zlib (zlib licence)")`, `add_subdirectory(tests)` under
   `BUILD_TESTING`.
3. `plugins/rpgmvp/package/README.txt.in` (install, what is supported, limitations; the
   placeholders `pvdkit_add_plugin` provides), `plugins/rpgmvp/README.md`,
   `plugins/rpgmvp/DESIGN.md` (the adapter, the describer, the fixtures, the exclusions),
   `plugins/rpgmvp/fixtures/SOURCES.md`.
4. `tests/guard/GuardTests.cpp`: extend the codec-header regex (`codecHeader`) with `spng.h` and
   add it to the "foreign headers are restricted to adapters and Exports" self-test.
5. Tests: `plugins/rpgmvp/tests/{core,adapters,e2e}` with `rpgmvp_core_tests`,
   `rpgmvp_adapter_tests` (own `add_executable`) and
   `pvdkit_add_plugin_e2e_tests(rpgmvp FIXTURES ... SOURCES ...)`, which also registers
   `rpgmvp_check_imports` and `rpgmvp_check_exports` (Release configuration).
Nothing under `src/`, `scripts/` or `CMakePresets.json` changes.

## Tests
Same structure as AVIF: adapter tests on fixtures (every kind, exact BGRA pixels for a few files,
16-bit → 8-bit rounding, tRNS → alpha, interlaced == non-interlaced pixels for the same image if a
pair exists — make one with ffmpeg), the detection function unit-tested exhaustively (CRC math with
hand-computed vectors), the describer's words for every colour type, e2e through
`LoadLibrary(RPGMVP.pvd)` in disk and memory mode incl. the `.png_` file, callback/abort, close
with un-freed page, concurrency, rejection list; VERSIONINFO read-back (shared test); x64 and
x86; coverage 100/100 on `plugins/rpgmvp/src/**` and shared code (`scripts/coverage.ps1`, both
presets, with every plugin enabled); `rpgmvp_check_imports` / `rpgmvp_check_exports`.

## Rules
As AGENTS.md: TDD, no commits, no worktrees, `PVDKIT_BUILD_SUFFIX=-t8`, zero warnings both
architectures, never touch `C:\Tools\FarManager`, do not run Far. Report with all outputs.

## Carried-over nits from the Task 7 review (do them as part of this task)
1. `cmake/pvdkit-plugin.cmake` — `pvdkit_add_plugin` must assert that the plugin `id` equals the
   directory name under `plugins/` (coverage.ps1 derives the id from the path); a mismatch must be
   a configure-time error, not a misleading coverage failure.
2. `scripts/coverage.ps1` — restrict the plugin id in the profile-name regex to `[A-Za-z0-9_]+`
   so an id like `avif-2` cannot match `avif`'s pattern.
3. `tests/guard/GuardTests.cpp` — reject a plugin header whose root-relative name also exists
   under shared `src/` (e.g. `plugins/<id>/src/core/Error.hpp` shadowing the shared one); add the
   self-test in both polarities.
4. Keep the AVIF `DESCRIPTION` as is ("PictureView (Far Manager)"); use the same wording for
   RPGMVP.
