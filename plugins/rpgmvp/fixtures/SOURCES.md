# RPGMVP fixture sources and expectations

All game inputs were copied with permission from the read-only sample directories named below and
renamed to descriptive ASCII names. Expectations were checked on the PNG produced by
`scripts/rpgmvp-decrypt.ps1` with ffprobe 9.0.1; exact pixels were checked by ffmpeg rawvideo output
and are asserted through the plugin's BGR/BGRA host output.

| Fixture | Source | Bytes | Reconstructed PNG expectation |
|---|---|---:|---|
| `rgba8_36x16_shadow2.rpgmvp` | `Awakening to Lust - RA26073439/www/img/system/Shadow2.rpgmvp` | 400 | 36x16, RGBA, depth 8, non-Adam7 |
| `indexed4_trns_144x192_cursor.rpgmvp` | `Awakening to Lust - RA26073439/www/img/characters/!$cursor_small.rpgmvp` | 529 | 144x192, indexed, depth 4, tRNS, non-Adam7 |
| `indexed8_trns_288x384_weapons3.rpgmvp` | `Awakening to Lust - RA26073439/www/img/system/Weapons3.rpgmvp` | 2,263 | 288x384, indexed, depth 8, tRNS, non-Adam7 |
| `rgb8_816x624_gameover.rpgmvp` | `Awakening to Lust - RA26073439/www/img/system/GameOver.rpgmvp` | 6,989 | 816x624, RGB, depth 8, non-Adam7 |
| `rgba16_60x20_par.rpgmvp` | `Awakening to Lust - RA26073439/www/img/menus/equip/Par.rpgmvp` | 21,981 | 60x20, RGBA, depth 16, non-Adam7 |
| `indexed8_trns_82x38_shadow2.png_` | `SpyBreak_Win_1.5/img/system/Shadow2.png_` | 463 | 82x38, indexed, depth 8, tRNS, non-Adam7; MZ extension/key variant |
| `rgba8_576x384_lolded.png_` | `SpyBreak_Win_1.5/img/characters/lolded.png_` | 3,727 | 576x384, RGBA, depth 8, non-Adam7; MZ extension |
| `rgba8_adam7_700x700_kamen.png_` | `Story by Story - Demo (PC)/img/pictures/Charakter/Ters/Kamen.png_` | 25,882 | 700x700, RGBA, depth 8, Adam7 |
| `rgb16_88x4a.rpgmvp` / `.png` | `ref/rpgmvp/rpgmvp/test/fixtures/rgb16_88x4a.*` | 19,537 / 19,521 | 88x4, RGB, depth 16, non-Adam7; decrypted bytes equal the PNG twin |
| `rgba8_48x48.rpgmvp` / `.png` | `ref/rpgmvp/rpgmvp/test/fixtures/rgba8_48x48.*` | 744 / 728 | 48x48, RGBA, depth 8, non-Adam7; decrypted bytes equal the PNG twin |

Synthetic accepted fixtures were produced by `scripts/make-synthetic-fixtures.ps1`, using ffmpeg
and the decrypt script's inverse `-Wrap` mode:

| Fixture | Bytes | Expectation |
|---|---:|---|
| `gray8_16x8.rpgmvp` | 112 | greyscale depth 8; BGR pixels (0,0) = `(0,0,0)`, (1,0) = `(16,16,16)`, (15,7) = `(240,240,240)` |
| `graya8_16x8.rpgmvp` | 128 | greyscale+alpha depth 8; BGRA pixel (2,1) = `(19,19,19,32)` |
| `gray1_16x8.rpgmvp` | 108 | greyscale depth 1; BGR pixels (0,0) = black and (8,0) = white |
| `rgba8_noninterlaced_700x700_kamen.rpgmvp` | 17,092 | non-Adam7 RGBA8 re-encoding; decoded pixels equal the Adam7 Kamen file |
| `rgba8_srgb_48x48.rpgmvp` | 757 | RGBA8 reference fixture with canonical sRGB intent-0 chunk inserted after IHDR |
| `apng_4x4.rpgmvp` | 245 | two-frame RGB8 APNG; deliberately reported and decoded as one default-image page |

The 4-bit indexed game fixture covers libspng's sub-byte palette-depth behavior. ffmpeg's PNG
encoder emitted `pal8` rather than a reliable 2-bit indexed PNG without pngquant, so no synthetic
2-bit palette fixture was added.

Exact conversion observations: the cursor's BGRA pixels (0,0) and (17,0) are `(0,0,0,0)` and
`(78,224,255,222)`. The RGBA16 source's first samples are R=`0xFFFF`, G=`0xFFFF`, B=`0xFFFF`,
A=`0x0000`, yielding little-endian BGRA16 `(0xFFFF,0xFFFF,0xFFFF,0x0000)`. The RGB16 source begins
with big-endian samples R=`0x1B96`, G=`0x6060`, B=`0x3BF4`; libspng 0.7.4's 16-to-8 reduction
yields BGR `(59,96,27)`, while BGRA16 is `(0x3BF4,0x6060,0x1B96,0xFFFF)`, both asserted exactly.

Negative fixtures derived from `rgba8_48x48.rpgmvp` are `stub_31.bin` (31 bytes), `stub_48.bin`
(48 bytes), `bad_ihdr_crc.rpgmvp` (744 bytes, one IHDR CRC byte changed),
`decode-failures/truncated_idat.rpgmvp` (200 bytes, valid IHDR and truncated IDAT), and
`decode-failures/bad_srgb_crc.rpgmvp` (757 bytes, valid IHDR and corrupt sRGB CRC), and
`too_large_100000x100000.rpgmvp` (744 bytes, valid recomputed IHDR CRC). The two plain `.png` twins
are also rejection fixtures. Short/signature/CRC/plain/limit inputs fail at open; the truncated IDAT
opens and fails cleanly at decode.
