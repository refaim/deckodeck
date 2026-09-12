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
3. Plain PNG (`89 50 4E 47`) → NotAvif-equivalent (`NotRecognised`; rename the ErrorCode in shared
   core to something format-neutral as part of this task or Task 7 — `NotAvif` is AVIF-specific).
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
  Chroma/CICP fields: whatever the Task 7 generalisation decided (e.g. `std::optional`).
- Output: **always BGRA32** (spec §4.2 — sprites/tilesets with alpha), pitch = width × 4,
  top-down, straight alpha. `PageInfo::bitsPerPixel` = depth × channels of the source (e.g. 32 for
  RGBA8, 64 for RGBA16, 8 for indexed8, 4 for indexed4) — informational, like AVIF.
- `ImageInfo`: `formatName` "RPGMVP" for `.rpgmvp`-style files — we cannot see the extension, so
  use "RPGMVP" always; `compression` "Deflate"; `comments` from a small describer:
  `"RPG Maker MV/MZ encrypted PNG, 8-bit RGBA, interlaced, tRNS"` (colour type name, depth,
  `interlaced` if Adam7, `palette` if indexed, `tRNS` if present, `PNG header version 0.3.1`).
- Plugin info: priority 10, name "RPGMVP", version "1.0.0", comments
  `"RPG Maker MV/MZ encrypted PNG decoder: libspng <ver>, zlib <ver>; static build"`.

## Fixtures (`plugins/rpgmvp/fixtures/`, commit them, SOURCES.md with provenance + expected values)
From Roma's sample games (`C:\Users\Roma\Desktop\rpgmvp`, permission given for small sprites), one
per PNG kind, keep the smallest; verify each with ffprobe after decrypting with the 32-byte rule and
record width/height/colour type/depth/interlace/tRNS and a few exact pixel values (decrypt with a
script in `scripts/` that is also committed — `rpgmvp-decrypt.ps1`, pure PowerShell, 32-byte rule):
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

## Tests
Same structure as AVIF: adapter tests on fixtures (every kind, exact BGRA pixels for a few files,
16-bit → 8-bit rounding, tRNS → alpha, interlaced == non-interlaced pixels for the same image if a
pair exists — make one with ffmpeg), the detection function unit-tested exhaustively (CRC math with
hand-computed vectors), e2e through `LoadLibrary(RPGMVP.pvd)` in disk and memory mode incl. the
`.png_` file, callback/abort, close with un-freed page, concurrency, rejection list; VERSIONINFO
read-back; x64 and x86; coverage 100/100 on `plugins/rpgmvp/src/**` and shared code; check-imports.

## Rules
As AGENTS.md: TDD, no commits, no worktrees, `PVDKIT_BUILD_SUFFIX=-t8`, zero warnings both
architectures, never touch `C:\Tools\FarManager`, do not run Far. Report with all outputs.
