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
| `icc_swapped_rb_64x64.png` / `.rpgmvp` | 2,586 / 2,602 | RGB8 quadrants: red, green, blue, mid-grey; embeds the generated 2,560-byte test profile with the `rXYZ` and `bXYZ` payloads swapped; adapter pixels remain exact BGR and unchanged by the profile |
| `icc_srgb_64x64.rpgmvp` | 2,602 | Same exact quadrants with the generated profile's normal sRGB colourants as the colour-management control |

The 4-bit indexed game fixture covers libspng's sub-byte palette-depth behavior. ffmpeg's PNG
encoder emitted `pal8` rather than a reliable 2-bit indexed PNG without pngquant, so no synthetic
2-bit palette fixture was added.

The ICC bytes are generated from scratch by `scripts/make-synthetic-fixtures.ps1`; they contain no
third-party profile bytes and are MIT-licensed as part of pvdkit. The deterministic ICC v2.1 display
profile identifies itself as `pvdkit sRGB-equivalent test profile`, uses D50 for its header and
`wtpt`, and uses the standard Bradford-adapted D50 sRGB `rXYZ`, `gXYZ`, and `bXYZ` colourants. Its
three TRC entries share one 1,024-sample `curv` payload implementing the piecewise sRGB transfer
function instead of approximating it with gamma 2.2. The swapped variant exchanges only the
equal-sized `rXYZ` and `bXYZ` payloads. The zero profile ID remains valid for this ICC v2 profile.
Quoted `exiftool "-ICC_Profile:all"` verification reports signature `acsp`, version 2.1.0, the
profile description and MIT copyright; the swapped profile reports red
`(0.1431, 0.06059, 0.7141)` and blue `(0.4361, 0.2225, 0.0139)`, the reverse of the control. The
committed `.png` twin retains the swapped profile so GDI+'s no-colour-management path is the
like-for-like Far reference.

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

## Synthetic encrypted WebP fixtures

The six `webp_*` fixtures were generated on 2026-11-11 with the installed Pillow 13.0.0 linked
against libwebp 1.6.0. They contain synthetic pixels rather than game assets. `Image.save` used
`format="WEBP"`, `lossless=True, exact=True` for lossless files, and `quality=80` for lossy files
(`exact=True` on the alpha input). The 2x2 opaque source contains red, green, blue and white in
row-major order. Its RGBA twin contains `(255,0,0,255)`, `(0,255,0,128)`, `(0,0,255,0)` and
`(17,34,51,64)`, preserving RGB even under zero alpha. Lossy 4x4 inputs are uniform red and
uniform `(90,160,230,128)` respectively.

Each plain WebP was wrapped as the standard header
`52 50 47 4d 56 00 00 00 00 03 01 00 00 00 00 00`, its first 16 bytes XORed with the synthetic
key `00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f`, and the unchanged remainder. Tests use
neither this key nor an adjacent game configuration file.

| Fixture | Bytes | Container and verified expectation |
|---|---:|---|
| `webp_rgb_lossless_2x2.png_` | 68 | VP8L, 2x2 RGB; exact red/green/blue/white pixels |
| `webp_rgba_lossless_2x2.png_` | 84 | VP8L, 2x2 straight RGBA; all four source pixels preserved exactly, including RGB under zero alpha |
| `webp_rgb_lossy_4x4.png_` | 84 | VP8, 4x4 RGB; decoded RGB is `(255,1,0)` throughout |
| `webp_rgba_lossy_4x4.png_` | 116 | VP8X with ALPH and VP8, 4x4 straight RGBA; decoded pixels are `(89,160,227,128)` throughout |
| `webp_metadata_2x2.png_` | 750 | VP8X with ICC/EXIF/XMP and lossless pixels; exact opaque 2x2 colours; bounded header-probe compression label is `WebP` |
| `webp_animation_2x2.png_` | 170 | VP8X animation with two 2x2 frames, 100 ms each and infinite loop; refused as `UnsupportedFeature` |

The metadata profile was generated by
`ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()`. The EXIF chunk payload is
`49 49 2a 00 08 00 00 00 00 00 00 00 00 00` (passed to Pillow with an `Exif\0\0` prefix); XMP is
`<x:xmpmeta xmlns:x="adobe:ns:meta/"/>`. The animation uses the opaque 2x2 image and a black second
frame with `save_all=True, duration=100, loop=0, lossless=True`. Profile and metadata presence
do not imply application by the plugin.

SHA-256 of the committed encrypted files:

```text
7E628D236CA1B863116A12B9A9BB2F124E293C88D6839BA6DE1A879F0A3522EF  webp_rgb_lossless_2x2.png_
978A41B252C7ADEB7280B4425DB27776A8C4CE68A28CA67179918B29C3449F37  webp_rgba_lossless_2x2.png_
1D71CEE7578F4F0E37D35D58DAD81E88B5C4B373E24E49F0D14BA7BE9B5D16E8  webp_rgb_lossy_4x4.png_
35D7587DE5330AF6DDC41C95EA4829870E8680FEE4733E2AE61F9FB74F885648  webp_rgba_lossy_4x4.png_
4642468D8462D5F608B8A629D35780562DCA22CF9719E6EEA7E9E512C168326D  webp_metadata_2x2.png_
C9580F22F5D1838D074FF468A8CDCDDAAD09AADEA0C890045428C307DEE337CE  webp_animation_2x2.png_
```
