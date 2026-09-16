"""Generates the synthetic OpenEXR fixtures of plugins/exr/fixtures and the reference table
tests/support/ReferencePixels.hpp. Driven by make-synthetic-fixtures.ps1 through
`uv run --with OpenEXR==3.4.13 --with numpy==2.3.3`; every picture is a formula, so the output is
byte-for-byte reproducible for one library version (SHA-256 of every file goes to SOURCES.md).

The reference table records, for every accepted fixture (synthetic and pinned corpus alike, except
the luminance-chroma ones whose reference is the C++ library's own RgbaInputFile), the header facts
the colour path depends on, the 99.99th-percentile peak computed here from the float data, and the
raw scene-linear RGBA values at a few display positions - decoded back through the library so lossy
schemes are measured, not assumed. The adapter test turns those into expected host pixels with its
own double-precision pipeline.
"""

import math
import os
import struct
import sys

import numpy as np
import OpenEXR

FIXTURES = sys.argv[1]
REFERENCE_HEADER = sys.argv[2]
CORPUS = sys.argv[3:]

REC709 = (0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290)
REC2020 = (0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290)
P3D65 = (0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290)
AP0 = (0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.0770, 0.32168, 0.33767)
AP1 = (0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767)
CUSTOM = (0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001, 0.3457, 0.3585)  # ProPhoto-like, D50

SCANLINE = OpenEXR.scanlineimage
TILED = OpenEXR.Storage.tiledimage


def ramp(width, height, scale=1.0, bright=None):
    """A photo-like scene: a horizontal ramp in R, vertical in G, diagonal in B, plus one bright
    highlight block so the tone-mapping peak is above 100 nit."""
    ys, xs = np.mgrid[0:height, 0:width].astype(np.float64)
    r = (xs + 0.5) / width * scale
    g = (ys + 0.5) / height * scale
    b = (xs + ys + 1.0) / (width + height) * scale
    if bright is not None:
        value, (x0, y0, x1, y1) = bright
        r[y0:y1, x0:x1] = value
        g[y0:y1, x0:x1] = value * 0.8
        b[y0:y1, x0:x1] = value * 0.6
    return r, g, b


def header(compression=OpenEXR.ZIP_COMPRESSION, storage=SCANLINE, **extra):
    result = {'compression': compression, 'type': storage}
    result.update(extra)
    return result


def write(name, hdr, channels):
    path = os.path.join(FIXTURES, name)
    with OpenEXR.File(hdr, channels) as f:
        f.write(path)
    return path


def write_parts(name, parts):
    path = os.path.join(FIXTURES, name)
    with OpenEXR.File(parts) as f:
        f.write(path)
    return path


def halves(r, g, b, a=None):
    channels = {'R': r.astype(np.float16), 'G': g.astype(np.float16), 'B': b.astype(np.float16)}
    if a is not None:
        channels['A'] = a.astype(np.float16)
    return channels


def tile_description(size, mode=OpenEXR.ONE_LEVEL):
    td = OpenEXR.TileDescription()
    td.xSize = size
    td.ySize = size
    td.mode = mode
    td.roundingMode = OpenEXR.ROUND_DOWN
    return td


generated = []  # (name, note): the notes fixtures/SOURCES.md quotes
references = []  # dicts for ReferencePixels.hpp


def note(name, text):
    generated.append((name, text))


# --- compression variants -----------------------------------------------------------------------

W, H = 16, 8
r, g, b = ramp(W, H, bright=(6.0, (10, 2, 14, 5)))
for suffix, compression in [('zip', OpenEXR.ZIP_COMPRESSION), ('none', OpenEXR.NO_COMPRESSION),
                            ('rle', OpenEXR.RLE_COMPRESSION), ('zips', OpenEXR.ZIPS_COMPRESSION),
                            ('piz', OpenEXR.PIZ_COMPRESSION), ('pxr24', OpenEXR.PXR24_COMPRESSION),
                            ('b44', OpenEXR.B44_COMPRESSION), ('b44a', OpenEXR.B44A_COMPRESSION),
                            ('dwaa', OpenEXR.DWAA_COMPRESSION), ('dwab', OpenEXR.DWAB_COMPRESSION),
                            ('htj2k256', OpenEXR.HTJ2K256_COMPRESSION), ('htj2k32', OpenEXR.HTJ2K32_COMPRESSION)]:
    write(f'rgb_half_{suffix}.exr', header(compression), halves(r, g, b))
    note(f'rgb_half_{suffix}.exr', f'16x8 RGB half, {suffix.upper()} compression, ramp with a 6.0 highlight (peak above 100 nit)')

# --- channel layouts ------------------------------------------------------------------------------

a = np.zeros((H, W))
a[:, 0:5] = 0.0
a[:, 5:11] = 0.5
a[:, 11:16] = 1.0
write('rgba_premultiplied.exr', header(), halves(r * a, g * a, b * a, a))
note('rgba_premultiplied.exr', '16x8 RGBA half, ZIP; alpha bands 0 / 0.5 / 1 across x, colour premultiplied by alpha')

y = (np.mgrid[0:H, 0:W][1].astype(np.float64) + 0.5) / W * 2.5
write('y_float_zips.exr', header(OpenEXR.ZIPS_COMPRESSION), {'Y': y.astype(np.float32)})
note('y_float_zips.exr', '16x8 Y only, FLOAT, ZIPS; horizontal ramp 0..2.5')

layers = {
    'aaa.Z': (r * 10).astype(np.float32),
    'beauty.R': r.astype(np.float16), 'beauty.G': g.astype(np.float16), 'beauty.B': b.astype(np.float16),
    'beauty.A': np.full((H, W), 0.75).astype(np.float16),
    'beauty.id': np.arange(H * W, dtype=np.uint32).reshape(H, W),
    'depth.R': np.zeros((H, W), np.float16), 'depth.G': np.zeros((H, W), np.float16),
    'depth.B': np.zeros((H, W), np.float16),
}
write('layer_rgb.exr', header(), layers)
note('layer_rgb.exr', '16x8, no top-level colour: layers aaa (Z only), beauty (RGBA half + UINT id) and depth (RGB); beauty is the first complete one')

write('uint_only.exr', header(), {'id': np.arange(H * W, dtype=np.uint32).reshape(H, W)})
note('uint_only.exr', '16x8 with one UINT channel: nothing displayable, refused')

write('rgb_float_mixed.exr', header(), {'R': r.astype(np.float32), 'G': g.astype(np.float16),
                                        'B': b.astype(np.float16), 'A': np.full((H, W), 1.0, np.float32)})
note('rgb_float_mixed.exr', '16x8 RGBA with mixed types (R and A FLOAT, G and B HALF), ZIP')

# --- windows ----------------------------------------------------------------------------------------

dr, dg, db = ramp(8, 6, bright=(3.0, (2, 2, 4, 4)))
write('data_inside_display.exr', header(dataWindow=((4, 3), (11, 8)), displayWindow=((0, 0), (15, 11))),
      halves(dr, dg, db, np.full((6, 8), 1.0)))
note('data_inside_display.exr', 'display 16x12 at 0,0; data 8x6 at 4,3 with alpha: transparent black around the data')

lr, lg, lb = ramp(16, 12, bright=(3.0, (4, 4, 6, 6)))
write('data_larger_than_display.exr', header(dataWindow=((0, 0), (15, 11)), displayWindow=((2, 2), (9, 7))),
      halves(lr, lg, lb))
note('data_larger_than_display.exr', 'display 8x6 at 2,2 inside data 16x12: the data is cropped')

pr, pg, pb = ramp(8, 8, bright=(3.0, (5, 5, 7, 7)))
write('data_offset_partial.exr', header(dataWindow=((-3, -2), (4, 5)), displayWindow=((0, 0), (7, 7))),
      halves(pr, pg, pb))
note('data_offset_partial.exr', 'display 8x8 at 0,0; data 8x8 at -3,-2: partial overlap, opaque black elsewhere')

write('data_disjoint.exr', header(dataWindow=((20, 20), (27, 27)), displayWindow=((0, 0), (7, 7))),
      halves(pr, pg, pb, np.full((8, 8), 1.0)))
note('data_disjoint.exr', 'display 8x8 at 0,0; data 8x8 at 20,20 with alpha: no overlap, the page is transparent black')

write('decreasing_y.exr', header(lineOrder=OpenEXR.DECREASING_Y), halves(r, g, b))
note('decreasing_y.exr', '16x8 RGB half, ZIP, lineOrder DECREASING_Y (same pixels as rgb_half_zip.exr)')

# --- exposure and colour ----------------------------------------------------------------------------

write('white_luminance_250.exr', header(whiteLuminance=250.0), halves(r, g, b))
note('white_luminance_250.exr', '16x8 RGB half, whiteLuminance 250: 1.0 is 250 nit')

for name, chroma in [('ap0', AP0), ('ap1', AP1), ('rec2020', REC2020), ('p3d65', P3D65), ('custom', CUSTOM)]:
    write(f'chroma_{name}.exr', header(chromaticities=chroma), halves(r, g, b))
    note(f'chroma_{name}.exr', f'16x8 RGB half with chromaticities {chroma}')

# The same ramp stored as CIE XYZ (OpenEXR's XYZ chromaticities, white E): the Rec.709 RGB to XYZ
# matrix at the D65 white, no adaptation, so the picture is rgb_half_zip.exr's and must present
# like it (the e2e test compares the two through the DLL).
XYZ = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0 / 3.0, 1.0 / 3.0)
rgb_to_xyz_709 = np.array([[0.4123908, 0.3575843, 0.1804808], [0.2126390, 0.7151687, 0.0721923], [0.0193308, 0.1191948, 0.9505322]])
xyz_stack = np.einsum('ij,jyx->iyx', rgb_to_xyz_709, np.stack([r, g, b]))
write('chroma_xyz.exr', header(chromaticities=XYZ), halves(xyz_stack[0], xyz_stack[1], xyz_stack[2]))
note('chroma_xyz.exr', '16x8 RGB half holding the CIE XYZ (D65) of the rgb_half_zip.exr ramp, chromaticities = CIE XYZ (primaries at the axes, white E): presented like rgb_half_zip.exr, no adaptation')

# A chromaticities attribute no derivation can use (the library itself throws on it): a white with
# y = 0. The plugin falls back to Rec.709 and says so; the pixels are rgb_half_zip.exr's.
WHITE_Y0 = (0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.0)
write('rgb_whitey0.exr', header(chromaticities=WHITE_Y0), halves(r, g, b))
note('rgb_whitey0.exr', '16x8 RGB half (the rgb_half_zip.exr ramp) with chromaticities whose white has y = 0: unusable, shown as Rec.709 with a note')

write('interop_lin_ap1.exr', header(colorInteropID='lin_ap1'), halves(r, g, b))
note('interop_lin_ap1.exr', '16x8 RGB half, colorInteropID lin_ap1 and no chromaticities: AP1 by the fallback')
write('interop_unknown.exr', header(colorInteropID='srgb_texture'), halves(r, g, b))
note('interop_unknown.exr', '16x8 RGB half, colorInteropID srgb_texture (not a linear id): Rec.709, the id is only reported')

hr, hg, hb = ramp(W, H, bright=(500.0, (10, 2, 14, 5)))
write('peak_above_10000.exr', header(), halves(hr, hg, hb))
note('peak_above_10000.exr', '16x8 RGB half with a 500.0 highlight (50000 nit): the peak clamps to 10000 nit')

nr, ng, nb = ramp(W, H)
nr = nr.astype(np.float32); ng = ng.astype(np.float32); nb = nb.astype(np.float32)
nr[0, 0] = np.nan; ng[0, 1] = np.inf; nb[0, 2] = -np.inf; nr[0, 3] = -0.5; ng[0, 4] = np.nan; nb[0, 5] = np.inf
write('nan_inf_negative.exr', header(), {'R': nr, 'G': ng, 'B': nb})
note('nan_inf_negative.exr', '16x8 RGB FLOAT, ZIP; row 0 carries NaN (R at x=0, G at x=4), +inf (G at x=1, B at x=5), -inf (B at x=2) and -0.5 (R at x=3)')

# --- limits and hostile files ---------------------------------------------------------------------------

tiny = np.full((2, 2), 0.5, np.float16)
write('huge_display.exr', header(dataWindow=((0, 0), (1, 1)), displayWindow=((0, 0), (39999, 39999))),
      {'R': tiny, 'G': tiny, 'B': tiny})
note('huge_display.exr', 'display 40000x40000 with a 2x2 data window: refused, the side exceeds the 32768 limit')
write('display_over_maxpixels.exr', header(dataWindow=((0, 0), (1, 1)), displayWindow=((0, 0), (19999, 19999))),
      {'R': tiny, 'G': tiny, 'B': tiny})
note('display_over_maxpixels.exr', 'display 20000x20000 (400 Mpx) with a 2x2 data window: refused, the area exceeds 16384x16384')

write('tile_too_large.exr', header(storage=TILED, tiles=tile_description(4097)), {'R': tiny, 'G': tiny, 'B': tiny})
note('tile_too_large.exr', '2x2 RGB in a single 4097x4097 tile: refused, the tile side exceeds the 4096 limit the plugin sets on the Core')

with open(os.path.join(FIXTURES, 'rgb_half_zip.exr'), 'rb') as f:
    zip_bytes = f.read()
with open(os.path.join(FIXTURES, 'truncated.exr'), 'wb') as f:
    f.write(zip_bytes[: len(zip_bytes) * 6 // 10])
note('truncated.exr', 'the first 60 % of rgb_half_zip.exr: the header parses, the chunk data is missing')

corrupt = bytearray(zip_bytes)
for offset in range(len(corrupt) - 40, len(corrupt) - 8):
    corrupt[offset] ^= 0x5A
with open(os.path.join(FIXTURES, 'corrupt_chunk.exr'), 'wb') as f:
    f.write(bytes(corrupt))
note('corrupt_chunk.exr', 'rgb_half_zip.exr with 32 bytes inside its only chunk flipped: the header parses, the deflate stream fails its checksum')

with open(os.path.join(FIXTURES, 'magic_only.exr'), 'wb') as f:
    f.write(b'\x76\x2f\x31\x01\x02\x00\x00\x00' + bytes((i * 37 + 11) & 0xFF for i in range(56)))
note('magic_only.exr', 'the OpenEXR magic number and version word followed by 56 bytes of nonsense: recognised, then refused')

rng = np.random.default_rng(0x455852)  # 'EXR'
with open(os.path.join(FIXTURES, 'garbage.bin'), 'wb') as f:
    f.write(rng.integers(0, 256, 4096, dtype=np.uint8).tobytes())
note('garbage.bin', '4096 pseudo-random bytes (seed 0x455852): not recognised')
with open(os.path.join(FIXTURES, 'not_exr.txt'), 'wb') as f:
    f.write(b'This is not an OpenEXR file.\r\n')
note('not_exr.txt', 'a text file: not recognised')

# --- parts and views ------------------------------------------------------------------------------------

vr, vg, vb = ramp(8, 8, bright=(2.0, (2, 2, 4, 4)))
right = OpenEXR.Part(header(view='right'), halves(vr, vg * 0.0, vb * 0.0), name='right')
left = OpenEXR.Part(header(view='left'), halves(vr, vg, vb), name='left')
write_parts('multipart_views.exr', [right, left])
note('multipart_views.exr', 'two flat parts: part 0 "right" (view right, only R lit) and part 1 "left" (view left, full ramp): the left view is shown')

deep = np.empty((8, 8), dtype=object)
for yy in range(8):
    for xx in range(8):
        deep[yy, xx] = np.array([0.5, 0.25][: (xx % 2) + 1], dtype=np.float16)
deep_part = OpenEXR.Part(header(OpenEXR.ZIPS_COMPRESSION, OpenEXR.Storage.deepscanline),
                         {'R': deep, 'G': deep, 'B': deep}, name='deep')
flat_part = OpenEXR.Part(header(), halves(vr, vg, vb), name='flat')
write_parts('multipart_deep_flat.exr', [deep_part, flat_part])
note('multipart_deep_flat.exr', 'part 0 "deep" (deepscanline RGB) and part 1 "flat" (8x8 RGB): the flat part is shown, deep parts skipped')

write('deep_only.exr', header(OpenEXR.ZIPS_COMPRESSION, OpenEXR.Storage.deepscanline), {'R': deep, 'G': deep, 'B': deep})
note('deep_only.exr', 'a single deepscanline part: refused (deep-only)')

deep_tiled_part = OpenEXR.Part(header(OpenEXR.ZIPS_COMPRESSION, OpenEXR.Storage.deeptile, tiles=tile_description(4)),
                               {'R': deep, 'G': deep, 'B': deep}, name='deep')
# A Part object is consumed by the write that used it (its header is finalised): a fresh flat part.
write_parts('multipart_deeptile_flat.exr', [deep_tiled_part, OpenEXR.Part(header(), halves(vr, vg, vb), name='flat')])
note('multipart_deeptile_flat.exr', 'part 0 "deep" (deeptile RGB, 4x4 tiles) and part 1 "flat" (8x8 RGB): the flat part is shown, deep parts skipped')

write('multiview_string.exr', header(multiView='left'), halves(vr, vg, vb))
note('multiview_string.exr', 'single part whose multiView attribute is a plain string, not a string vector: ignored, the bare RGB is shown')

stereo_left_first = dict(halves(vr, vg, vb))
stereo_left_first.update({'right.R': vr.astype(np.float16), 'right.G': (vg * 0.0).astype(np.float16), 'right.B': (vb * 0.0).astype(np.float16)})
write('multiview_left_first.exr', header(multiView=['left', 'right']), stereo_left_first)
note('multiview_left_first.exr', 'single part, multiView [left, right]: the bare channels are the left view and are shown')

stereo_right_first = {'R': vr.astype(np.float16), 'G': (vg * 0.0).astype(np.float16), 'B': (vb * 0.0).astype(np.float16),
                      'left.R': vr.astype(np.float16), 'left.G': vg.astype(np.float16), 'left.B': vb.astype(np.float16)}
write('multiview_right_first.exr', header(multiView=['right', 'left']), stereo_right_first)
note('multiview_right_first.exr', 'single part, multiView [right, left]: the bare channels are the right view, the left layer is shown')

# --- tiles --------------------------------------------------------------------------------------------------

write('tiled_rgba_3x3.exr', header(storage=TILED, tiles=tile_description(3)),
      halves(vr, vg, vb, np.full((8, 8), 0.5)))
note('tiled_rgba_3x3.exr', '8x8 RGBA half in 3x3 tiles (partial tiles at the right and bottom), one level')

write('tiled_data_offset.exr', header(storage=TILED, tiles=tile_description(4), dataWindow=((5, 3), (12, 10)),
                                      displayWindow=((0, 0), (15, 15))), halves(vr, vg, vb))
note('tiled_data_offset.exr', '8x8 RGB half in 4x4 tiles, data at 5,3 inside a 16x16 display window')

# --- luminance/chroma -----------------------------------------------------------------------------------
# The binding takes every channel at full resolution and writes the 2x2-sampled ones at the even
# positions. Y is a horizontal ramp, RY/BY smooth (the values are what RgbaYca reconstructs from,
# the adapter test compares against the C++ library, not against these arrays).
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
luma = ((xx + 0.5) / W * 1.5).astype(np.float16)
ry = (0.15 * np.cos(yy / H * math.pi)).astype(np.float16)
by = (-0.1 * np.sin(xx / W * math.pi)).astype(np.float16)


def luminance_chroma(extra=None, alpha=None):
    channels = {'Y': OpenEXR.Channel('Y', luma), 'RY': OpenEXR.Channel('RY', ry, 2, 2), 'BY': OpenEXR.Channel('BY', by, 2, 2)}
    if alpha is not None:
        channels['A'] = OpenEXR.Channel('A', alpha.astype(np.float16))
    if extra is not None:
        channels.update(extra)
    return channels


write('chroma_zips.exr', header(OpenEXR.ZIPS_COMPRESSION), luminance_chroma(alpha=np.ones((H, W))))
note('chroma_zips.exr', '16x8 half Y/RY/BY with A = 1, ZIPS: one line per chunk, so the odd chunks carry no chroma row')

write('chroma_extra_channel.exr', header(), luminance_chroma(extra={'Z': OpenEXR.Channel('Z', (yy / H).astype(np.float32))}))
note('chroma_extra_channel.exr', '16x8 half Y/RY/BY plus a float Z channel, ZIP: Z is skipped in the luminance/chroma path')

write('chroma_missing_by.exr', header(), {'Y': OpenEXR.Channel('Y', luma), 'RY': OpenEXR.Channel('RY', ry, 2, 2)})
note('chroma_missing_by.exr', '16x8 half Y and RY without BY, ZIP: no chroma pair, Y alone is shown as grey')

with open(os.path.join(FIXTURES, 'chroma_extra_channel.exr'), 'rb') as f:
    chroma_bytes = bytearray(f.read())
for offset in range(len(chroma_bytes) - 40, len(chroma_bytes) - 8):
    chroma_bytes[offset] ^= 0x5A
with open(os.path.join(FIXTURES, 'chroma_corrupt_chunk.exr'), 'wb') as f:
    f.write(bytes(chroma_bytes))
note('chroma_corrupt_chunk.exr', 'chroma_extra_channel.exr with 32 bytes inside its only chunk flipped: the luminance/chroma decode fails on the chunk')

# Y/RY/BY with the two chromaticities sets Imf::RGBtoXYZ throws on: the plugin must neither
# call the library with them nor let anything throw; it reconstructs and presents as Rec.709
# (the pixels are chroma_extra_channel.exr's) and says so in the info line.
COLLINEAR = (0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3127, 0.3290)
write('yc_whitey0.exr', header(chromaticities=WHITE_Y0), luminance_chroma())
note('yc_whitey0.exr', '16x8 half Y/RY/BY (chroma_extra_channel.exr without Z) with chromaticities whose white has y = 0: unusable, reconstructed and shown as Rec.709 with a note')
write('yc_collinear.exr', header(chromaticities=COLLINEAR), luminance_chroma())
note('yc_collinear.exr', '16x8 half Y/RY/BY with three equal primaries: unusable, reconstructed and shown as Rec.709 with a note')


def patch_box2i(data, name, box):
    """Rewrites the box2i attribute `name` of a header in place ((xmin, ymin), (xmax, ymax))."""
    key = name.encode('ascii') + b'\x00box2i\x00'
    pos = data.find(key)
    assert pos >= 0, name
    size_pos = pos + len(key)
    assert struct.unpack_from('<i', data, size_pos)[0] == 16
    struct.pack_into('<4i', data, size_pos + 4, box[0][0], box[0][1], box[1][0], box[1][1])


# A hostile luminance/chroma header: a 20000x20000 data window (400 Mpx, past the 16384x16384
# pixel limit; the sides are within the 32768 dimension limit) behind a 1x1 display window, with
# chroma_zips.exr's 8 chunks behind it. The RGB path would crop it; the luminance/chroma path
# reconstructs over the whole data window, so the plugin refuses it before allocating (TooLarge).
with open(os.path.join(FIXTURES, 'chroma_zips.exr'), 'rb') as f:
    huge = bytearray(f.read())
patch_box2i(huge, 'dataWindow', ((0, 0), (19999, 19999)))
patch_box2i(huge, 'displayWindow', ((0, 0), (0, 0)))
with open(os.path.join(FIXTURES, 'yc_huge_data.exr'), 'wb') as f:
    f.write(bytes(huge))
note('yc_huge_data.exr', 'chroma_zips.exr with its header patched to a 20000x20000 data window behind a 1x1 display window: refused before any allocation (the luminance/chroma path reads the whole data window)')


# --- reference table ----------------------------------------------------------------------------------------

def usable(chroma):
    """The plugin's rule (core::colour::Primaries::isUsable): finite, white y > 0, primaries not
    collinear; anything else is shown as Rec.709."""
    rx, ry, gx, gy, bx, by, wx, wy = chroma
    if not all(math.isfinite(v) for v in chroma) or not wy > 0:
        return False
    return rx * (gy - by) + gx * (by - ry) + bx * (ry - gy) != 0


def y_weights(chroma):
    """The Y row of RGB to XYZ in the column form (nothing divides by a primary's y, so the CIE XYZ
    set is as valid as any other)."""
    rx, ry, gx, gy, bx, by, wx, wy = chroma
    m = np.array([[rx, gx, bx], [ry, gy, by], [1 - rx - ry, 1 - gx - gy, 1 - bx - by]])
    white = np.array([wx / wy, 1.0, (1 - wx - wy) / wy])
    scales = np.linalg.solve(m, white)
    return m[1] * scales


def sanitize(v):
    v = np.array(v, dtype=np.float64)
    v = np.where(np.isnan(v), 0.0, v)
    return np.where(v > 0, v, 0.0)


def positions_for(width, height):
    return [(0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1), (width // 2, height // 2),
            (width // 3, height // 4), (2 * width // 3, 3 * height // 4)]


def reference_for(path, name):
    """Reads the fixture back and records its colour facts, peak and raw samples."""
    with OpenEXR.File(path, separate_channels=True) as f:
        parts = f.parts
        # The part the plugin shows: the left view if any part carries view=left, else the first
        # non-deep part with colour channels (the plugin's rule; deep parts are skipped).
        chosen = None
        for p in parts:
            if p.header.get('type') in (OpenEXR.Storage.deepscanline, OpenEXR.Storage.deeptile):
                continue
            if p.header.get('view') == 'left':
                chosen = p
                break
            if chosen is None:
                chosen = p
        # Copied out: the binding releases the channel data when the file closes.
        hdr = dict(chosen.header)
        chans = {n: c.pixels.copy() for n, c in chosen.channels.items()}
    names = set(chans.keys())
    multi = hdr.get('multiView')
    prefix = ''
    if multi is not None and multi[0] != 'left' and {'left.R', 'left.G', 'left.B'} <= names:
        prefix = 'left.'
    if not prefix and not {'R', 'G', 'B'} <= names:
        layer_prefixes = sorted({n[: n.rfind('.') + 1] for n in names if '.' in n})
        for lp in layer_prefixes:
            if {lp + 'R', lp + 'G', lp + 'B'} <= names:
                prefix = lp
                break
    grey = False
    if {prefix + 'R', prefix + 'G', prefix + 'B'} <= names:
        R = chans[prefix + 'R'].astype(np.float64)
        G = chans[prefix + 'G'].astype(np.float64)
        B = chans[prefix + 'B'].astype(np.float64)
        alpha_name = prefix + 'A'
    elif 'Y' in names:
        R = G = B = chans['Y'].astype(np.float64)
        grey = True
        alpha_name = 'A'
    else:
        first = sorted(n for n in names if chans[n].dtype != np.uint32)[0]
        R = G = B = chans[first].astype(np.float64)
        grey = True
        alpha_name = 'A'
    has_alpha = alpha_name in names
    A = chans[alpha_name].astype(np.float64) if has_alpha else np.ones_like(R)
    dw = hdr['dataWindow']
    disp = hdr['displayWindow']
    chroma = hdr.get('chromaticities')
    interop = hdr.get('colorInteropID', '')
    if chroma is None:
        chroma = {'lin_rec709': REC709, 'lin_rec2020': REC2020, 'lin_p3d65': P3D65, 'lin_ap0': AP0, 'lin_ap1': AP1}.get(interop, REC709)
    chroma = tuple(float(c) for c in chroma)
    if not usable(chroma):
        chroma = REC709
    white = float(hdr.get('whiteLuminance', 100.0))
    if not (math.isfinite(white) and white > 0):
        white = 100.0

    # Straight colour in nits, luminance with the file's own weights, then the percentile over the
    # display window (pixels outside the data window are black).
    a = np.clip(sanitize(A), 0.0, 1.0)
    rs, gs, bs = sanitize(R) * white, sanitize(G) * white, sanitize(B) * white
    divide = a > 0
    rs = np.where(divide, rs / np.where(divide, a, 1.0), rs)
    gs = np.where(divide, gs / np.where(divide, a, 1.0), gs)
    bs = np.where(divide, bs / np.where(divide, a, 1.0), bs)
    w = y_weights(chroma)
    lum = sanitize(w[0] * rs + w[1] * gs + w[2] * bs)
    dw_w = int(dw[1][0]) - int(dw[0][0]) + 1
    dw_h = int(dw[1][1]) - int(dw[0][1]) + 1
    ox0, oy0 = max(int(dw[0][0]), int(disp[0][0])), max(int(dw[0][1]), int(disp[0][1]))
    ox1, oy1 = min(int(dw[1][0]), int(disp[1][0])), min(int(dw[1][1]), int(disp[1][1]))
    disp_w = int(disp[1][0]) - int(disp[0][0]) + 1
    disp_h = int(disp[1][1]) - int(disp[0][1]) + 1
    positions = positions_for(disp_w, disp_h)
    total = disp_w * disp_h
    if ox1 >= ox0 and oy1 >= oy0:
        overlap = lum[oy0 - int(dw[0][1]): oy1 - int(dw[0][1]) + 1, ox0 - int(dw[0][0]): ox1 - int(dw[0][0]) + 1].ravel()
    else:
        overlap = np.zeros(0)
    values = np.concatenate([np.zeros(total - overlap.size), overlap])
    values.sort()
    rank = math.ceil(0.9999 * total)
    peak = float(min(max(values[rank - 1], 100.0), 10000.0))

    samples = []
    for (x, y) in positions:
        ax, ay = x + int(disp[0][0]), y + int(disp[0][1])
        inside = int(dw[0][0]) <= ax <= int(dw[1][0]) and int(dw[0][1]) <= ay <= int(dw[1][1])
        if inside:
            ix, iy = ax - int(dw[0][0]), ay - int(dw[0][1])
            samples.append((x, y, True, float(R[iy, ix]), float(G[iy, ix]), float(B[iy, ix]), float(A[iy, ix])))
        else:
            samples.append((x, y, False, 0.0, 0.0, 0.0, 0.0))
    references.append({'name': name, 'width': disp_w, 'height': disp_h, 'alpha': has_alpha, 'grey': grey,
                       'white': white, 'chroma': chroma, 'peak': peak, 'samples': samples})



def is_luminance_chroma(path):
    """Y/RY/BY files are referenced against the C++ RgbaInputFile in the adapter test, not here."""
    with OpenEXR.File(path, separate_channels=True) as f:
        names = set(f.parts[0].channels.keys())
    return {'RY', 'BY'} <= names


accepted = [n for n, _ in generated if n.endswith('.exr') and n not in (
    'uint_only.exr', 'huge_display.exr', 'display_over_maxpixels.exr', 'truncated.exr', 'corrupt_chunk.exr',
    'magic_only.exr', 'deep_only.exr', 'tile_too_large.exr', 'chroma_corrupt_chunk.exr', 'yc_huge_data.exr')]
for name in accepted:
    path = os.path.join(FIXTURES, name)
    if is_luminance_chroma(path):
        continue
    try:
        reference_for(path, name)
    except Exception as error:
        raise RuntimeError(f"reference for {name}: {error}") from error

for path in CORPUS:
    if is_luminance_chroma(path):
        continue
    reference_for(path, os.path.basename(path))


def float_literal(v):
    if math.isnan(v):
        return 'kNaN'
    if math.isinf(v):
        return 'kInf' if v > 0 else '-kInf'
    return repr(float(v)) + ('' if 'e' in repr(float(v)) or '.' in repr(float(v)) else '.0')


lines = ['#pragma once', '', '// Generated by plugins/exr/scripts/make-synthetic-fixtures.ps1 (make_synthetic_fixtures.py);',
         '// do not edit. Raw scene-linear values of a few display pixels of every accepted fixture as the',
         '// OpenEXR Python binding decodes them, with the colour facts and the 99.99th-percentile peak the',
         '// generator computed independently of the plugin.', '', '#include <array>', '#include <cstdint>',
         '#include <limits>', '#include <span>', '#include <string_view>', '',
         'namespace pvdkit::exr::tests', '{', '',
         '    inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();',
         '    inline constexpr double kInf = std::numeric_limits<double>::infinity();', '',
         '    struct ReferenceSample', '    {', '        std::uint32_t x;', '        std::uint32_t y;',
         '        bool inside;', '        double r;', '        double g;', '        double b;', '        double a;',
         '    };', '',
         '    struct ReferenceFixture', '    {', '        std::string_view name;', '        std::uint32_t width;',
         '        std::uint32_t height;', '        bool alpha;', '        bool grey;', '        double whiteNits;',
         '        std::array<double, 8> chromaticities;', '        double peakNits;',
         '        std::span<const ReferenceSample> samples;', '    };', '']
for ref in references:
    ident = ref['name'].replace('.', '_').replace('-', '_')
    lines.append(f'    inline constexpr std::array<ReferenceSample, {len(ref["samples"])}> kSamples_{ident}{{{{')
    for (x, y, inside, rr, gg, bb, aa) in ref['samples']:
        lines.append(f'        ReferenceSample{{{x}, {y}, {"true" if inside else "false"}, {float_literal(rr)}, {float_literal(gg)}, {float_literal(bb)}, {float_literal(aa)}}},')
    lines.append('    }};')
lines.append('')
lines.append(f'    inline constexpr std::array<ReferenceFixture, {len(references)}> kReferenceFixtures{{{{')
for ref in references:
    ident = ref['name'].replace('.', '_').replace('-', '_')
    chroma = ', '.join(float_literal(c) for c in ref['chroma'])
    lines.append(f'        ReferenceFixture{{"{ref["name"]}", {ref["width"]}, {ref["height"]}, {"true" if ref["alpha"] else "false"}, '
                 f'{"true" if ref["grey"] else "false"}, {float_literal(ref["white"])}, {{{chroma}}}, {float_literal(ref["peak"])}, kSamples_{ident}}},')
lines.append('    }};')
lines.append('')
lines.append('} // namespace pvdkit::exr::tests')
with open(REFERENCE_HEADER, 'w', encoding='ascii', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')

for name, text in generated:
    print(name + '	' + text)
print(f'wrote {len(generated)} synthetic fixtures and {len(references)} references')
