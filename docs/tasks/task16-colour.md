# Task 16 — colour pipeline (`src/core/colour`) and HDR/wide-gamut presentation for AVIF

Goal: every plugin delivers **display-referred sRGB** to the host. Today the AVIF plugin hands the
host the samples as they were encoded: a PQ/HLG or Rec.2020/P3 file is shown dark, washed out or
oversaturated, because the host knows only sRGB. A shared, tested colour module converts any
CICP-described source to sRGB, applied in `FileSession` after `decodeFrame` (the same place the
clap/irot/imir `Transform` runs), so adapters stay library-only. The module is the foundation for
DPX (Cineon log), EXR (scene-linear) and JXL later; this task ships it with AVIF as the first
consumer. Roma's words: "показ должен быть пиздатый" — a viewer that opens a file but shows it
wrong has not opened it.

## The pipeline (float, per pixel; input and output are 16-bit BGRA64 rows)
1. **Decode the transfer** (CICP transfer characteristics, ITU-T H.273 code points) to linear
   light: 1/6/14/15 (BT.709/601/2020 — use BT.1886 EOTF, gamma 2.4 with L_B = 0), 13 (sRGB IEC
   61966-2-1 exact piecewise), 8 (linear), 4 (gamma 2.2), 5 (gamma 2.8), 16 (PQ, ST 2084, absolute
   nits, L_W = 10 000), 18 (HLG, BT.2100 with the OOTF for a 1000-nit reference display, system
   gamma 1.2), 2 (unspecified → treat as sRGB, no conversion). Anything else: `UnsupportedFeature`
   is NOT acceptable for a viewer — fall back to sRGB and record the fallback in the image
   comments (`ImageDescription`) so the info line says what happened.
2. **Primaries → BT.709/sRGB** (H.273 colour primaries): 1 (709 — identity), 9 (Rec.2020), 12
   (P3-D65), 11 (P3-DCI, white 0.314/0.351 → Bradford adaptation to D65), 5/6 (BT.470BG/601-625),
   7 (SMPTE 240M/601-525), 4 (BT.470M, Illuminant C → Bradford), 22 (EBU 3213-E), 2 (unspecified
   → identity). Matrices are computed at compile time (`constexpr`) from the chromaticities in
   H.273 Table 2 via the standard RGB→XYZ derivation and Bradford chromatic adaptation — do not
   paste magic matrices; assert against published reference matrices in tests (e.g. BT.2020→709
   from BT.2087, P3-D65→709) to 1e-4.
3. **Tone-map HDR to SDR** when the transfer was PQ or HLG: **BT.2390 EETF** (Rec. ITU-R
   BT.2390-10 §5.4.1, the Hermite-spline knee) from the source peak (PQ: the mastering peak if
   the file carries it — libavif exposes `clli`/`mdcv` boxes; else 1000 nits; HLG: 1000) to a
   100-nit, 0.005-nit-black reference display, applied on luminance (Y of BT.2020 for a 2020
   source) with ratio-preserving chroma (BT.2390 §5.4.2 "desaturation" variant, or the simpler
   per-channel maxRGB scaling — pick one, justify, and make the choice one function). Output
   linear normalised so 100 nits → 1.0.
4. **sRGB OETF** (exact piecewise), clamp to [0, 1], quantise to 16 bits with rounding.
5. **Range and matrix**: libavif already produces full-range RGB from the YUV (matrix
   coefficients + range applied inside libavif/libyuv) — verify and rely on it; the module works
   on RGB only. `Cicp::matrix` and `fullRange` are therefore informational here (comments).

Identity short-circuit: primaries 1 or 2 AND transfer 13, 2, 1, 6, 14, 15 with 8-bit output means
"already sRGB-ish" — **no conversion, byte-identical to today** (BT.709 vs sRGB transfer differ in
the toe; treat 1/6/14/15 as sRGB for SDR content like every other viewer does — document).
Conversion is applied when primaries ∉ {1, 2} or transfer ∈ {16, 18, 4, 5, 8}.

## Where it lives
- `src/core/colour/`: `Transfer.hpp/.cpp` (EOTF/OETF pairs, each a pure `float → float`),
  `Primaries.hpp/.cpp` (chromaticities, `Matrix3` constexpr, adaptation), `ToneMap.hpp/.cpp`
  (BT.2390 EETF), `Pipeline.hpp/.cpp` (`struct Presentation { … }` built from a `Cicp` + optional
  mastering peak; `apply(std::span<std::uint16_t> bgraRow)` in place, row by row — cache the
  per-channel transfer as a 65 536-entry LUT for 16-bit input; the primaries matrix and tone-map
  are per pixel in float; SIMD not required, but no per-pixel allocations).
- `core::ImageMeta` gains `std::optional<float> masteringPeakNits` (from AVIF `mdcv`/`clli`,
  `max_cll`/`max_display_mastering_luminance`; the adapter fills it when the box is present).
- `FileSession::decodePage`: after `decodeFrame` (and before/after `Transform` — colour first,
  transforms are colour-agnostic), if `Presentation::needed(meta)`, run the pipeline in place.
  For 8-bit sources needing conversion, `FileSession` requests `Bgra64` from the decoder anyway
  (`deepOutput || Presentation::needed(meta)`) so the pipeline has 16-bit headroom; the output
  stays 64 bpp.
- `ImageDescription`/comments: the AVIF describer appends `→ sRGB (BT.2390 tone map from PQ 1000 nit)`
  or `→ sRGB (Rec.2020 primaries)` when a conversion applies; nothing when identity.
- `docs/ARCHITECTURE.md` §3: the module, the placement, the identity rule.

## Tests (TDD, 100/100 on `src/**`)
- Transfer functions: every code point against published reference values (PQ: BT.2100 Table 4
  anchors, e.g. 0.5081 → 100 nits/10 000 = 0.01; HLG: 0.5 → 1/12 scene-linear; sRGB: 0.5 →
  0.214; BT.1886: 0.5 → 0.189) and round-trip EOTF∘OETF ≈ identity to 1e-5 across 4096 samples.
- Primaries: matrices vs BT.2087 (2020→709) and the P3-D65→709 matrix from SMPTE RP 431-2 /
  common references to 1e-4; white stays white (1,1,1 → 1,1,1 within 1e-5) for every primaries.
- Tone map: monotonic, identity below the knee, peak maps to 1.0, the BT.2390 worked example
  values (the standard gives a table for 1000→100 nit).
- Pipeline: identity short-circuit is byte-identical (existing AVIF exact-pixel tests must not
  change); HDR fixtures (`colors_hdr_rec2020.avif`, `cosmos1650_yuv444_10bpc_p3pq.avif`,
  `weld_sato_12B_8B_q0.avif` if PQ) produce plausible sRGB: assert exact values for a few pixels
  computed by an **independent reference** — ffmpeg (`C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin`)
  with `-vf zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709:t=bt709:m=bt709,tonemap=…`
  is NOT the same tone-map operator, so compare only the transfer+primaries stage against
  `zscale` output (tone map disabled, clip) to a tolerance of 2/255 on SDR-range pixels, and
  verify the tone-map stage with the standard's own numbers. Record the commands in the report.
- e2e: through the DLL, an HDR fixture yields 64 bpp with the converted values; the info line
  shows the conversion note.

## Versions and docs
AVIF → 1.2.0 (feature). README/README.txt.in: "HDR (PQ/HLG) and wide-gamut (Rec.2020, P3) images
are converted to sRGB for display (BT.2390 tone mapping); ICC profiles are not applied (see
Task 15)". RPGMVP unaffected (PNG carries no CICP beyond the sRGB chunk; `gAMA`/`cHRM` remain
ignored — document).

## Rules
`AGENTS.md`; the Addendum of `docs/tasks/task8-rpgmvp.md` (lint, leak, ASan, CPU etiquette);
`PVDKIT_BUILD_SUFFIX=-t16`; no commits; never touch `C:\Tools\FarManager`. Report
`docs/tasks/report-task16.md` with every gate's output on both architectures and the four Release
DLL hashes.
