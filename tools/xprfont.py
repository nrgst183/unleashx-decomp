#!/usr/bin/env python3
"""Reads and writes the XDK's bitmap font files (.xpr fonts: a texture, then
the glyph table), and makes sharper ones.

  python3 tools/xprfont.py info Font.xpr ...
  python3 tools/xprfont.py png Font.xpr out.png
  python3 tools/xprfont.py hires Arial.xpr Arial@2x.xpr [--scale 2] [--face arialbd.ttf ...]

"hires" writes the same font with a texture --scale times larger (2 or 4):
every glyph drawn again at that size from a TrueType face, in the same place,
so the glyph table (sizes, spacing, line height) is unchanged and the dash
lays text out exactly as before, only sharper on a screen larger than
640x480. The face's size and the font's drop shadow are worked out from the
old glyphs; with several --face files the closest is used. With none, it's
guessed from the file name (Arial.xpr: Arial or Arial Bold) among the
system's fonts. Glyphs the face lacks are the old ones, enlarged.

Needs Pillow (pip install pillow).
"""
import argparse
import os
import struct

XPR0 = 0x30525058

# Xbox texture formats the fonts use: (name, bits per pixel). All swizzled.
FORMATS = {
    0x00: ('L8', 8), 0x01: ('AL8', 8), 0x19: ('A8', 8),
    0x02: ('A1R5G5B5', 16), 0x04: ('A4R4G4B4', 16), 0x05: ('R5G6B5', 16),
    0x06: ('A8R8G8B8', 32), 0x07: ('X8R8G8B8', 32),
    0x1A: ('A8L8', 16),
}


def morton_tables(width, height):
    """The swizzled offset (in texels) of each x and each y, to be or'ed: the
    bits of x and y interleaved, x's first, the larger side's left over at
    the top."""
    wb, hb = width.bit_length() - 1, height.bit_length() - 1

    def spread(v, own, other, first):
        out, p = 0, 0
        for i in range(max(own, other)):
            for is_own in ((True, False) if first else (False, True)):
                if is_own and i < own:
                    out |= ((v >> i) & 1) << p
                    p += 1
                elif not is_own and i < other:
                    p += 1
        return out
    return ([spread(x, wb, hb, True) for x in range(width)],
            [spread(y, hb, wb, False) for y in range(height)])


class XprFont:
    def __init__(self, data):
        magic, total, header = struct.unpack_from('<III', data, 0)
        if magic != XPR0:
            raise ValueError('not an XPR0 file')
        self.header_size = header
        self.sys = bytearray(data[12:header])
        self.vid = bytearray(data[header:total])
        _, self.tex_offset, _, self.tex_format, size = struct.unpack_from('<IIIII', self.sys, 0)
        self.fmt = (self.tex_format >> 8) & 0xFF
        if self.fmt not in FORMATS or size != 0:
            raise ValueError('font texture format 0x%02X (size 0x%X) not handled' % (self.fmt, size))
        self.width = 1 << ((self.tex_format >> 20) & 0xF)
        self.height = 1 << ((self.tex_format >> 24) & 0xF)
        self.levels = max(1, (self.tex_format >> 16) & 0xF)
        self.bpp = FORMATS[self.fmt][1]
        f = 0x14   # (the font data follows the texture's header)
        self.version, = struct.unpack_from('<I', self.sys, f + 8)
        if self.version == 5:
            self.cell_height, = struct.unpack_from('<f', self.sys, f + 0x0C)
            self.line_height, = struct.unpack_from('<f', self.sys, f + 0x18)
            self.max_glyph, = struct.unpack_from('<H', self.sys, f + 0x1C)
            self.table = list(struct.unpack_from('<%dh' % (self.max_glyph + 1), self.sys, f + 0x1E))
            glyphs_at = f + 0x20 + self.max_glyph * 2
            self.num_glyphs, = struct.unpack_from('<I', self.sys, glyphs_at)
            glyphs_at += 4
        elif self.version == 4:
            height, = struct.unpack_from('<I', self.sys, f + 0x0C)
            self.cell_height = self.line_height = float(height)
            low, high = struct.unpack_from('<HH', self.sys, f + 0x1C)
            self.max_glyph = high
            self.table = [0 if c < low else c - low for c in range(high + 1)]
            self.num_glyphs = high - low + 1
            glyphs_at = f + 0x24
        else:
            raise ValueError('font version %d' % self.version)
        self.glyphs_at = glyphs_at
        self.glyphs = [struct.unpack_from('<ffffhhhh', self.sys, glyphs_at + i * 0x18)
                       for i in range(self.num_glyphs)]

    def characters(self):
        """{glyph: the first character that maps to it}."""
        out = {}
        for c, g in enumerate(self.table):
            if g not in out and (g != 0 or c == 0):
                out[g] = c
        return out

    def cell(self, g):
        """Glyph g's place in the texture, in texels."""
        tu1, tv1, tu2, tv2 = self.glyphs[g][:4]
        return (round(tu1 * self.width), round(tv1 * self.height), round(tu2 * self.width),
                round(tv2 * self.height))

    def decode(self, d, i):
        name = FORMATS[self.fmt][0]
        if name in ('A8R8G8B8', 'X8R8G8B8'):
            b, g, r, a = d[i], d[i + 1], d[i + 2], d[i + 3]
            return (a if name == 'A8R8G8B8' else 255, r, g, b)
        if name == 'A4R4G4B4':
            v = d[i] | d[i + 1] << 8
            return tuple(((v >> s) & 0xF) * 17 for s in (12, 8, 4, 0))
        if name == 'A1R5G5B5':
            v = d[i] | d[i + 1] << 8
            return (255 if v & 0x8000 else 0, ((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                    (v & 31) * 255 // 31)
        if name == 'R5G6B5':
            v = d[i] | d[i + 1] << 8
            return (255, (v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
        if name == 'L8':
            return (255, d[i], d[i], d[i])
        if name in ('A8', 'AL8'):
            return (d[i], d[i], d[i], d[i])
        if name == 'A8L8':
            return (d[i + 1], d[i], d[i], d[i])
        raise ValueError(name)

    def encode(self, a, r, g, b):
        name = FORMATS[self.fmt][0]
        if name == 'A4R4G4B4':
            # (Rounded, not cut: 4 bits keep the edges' shades closer.)
            q = lambda v: min(15, (v + 8) // 17)
            return struct.pack('<H', q(a) << 12 | q(r) << 8 | q(g) << 4 | q(b))
        if name in ('A8R8G8B8', 'X8R8G8B8'):
            return bytes((b, g, r, a))
        raise ValueError('writing %s fonts is not handled' % name)

    def image(self):
        """The texture's top level as an RGBA image."""
        from PIL import Image
        img = Image.new('RGBA', (self.width, self.height))
        px = img.load()
        xs, ys = morton_tables(self.width, self.height)
        step = self.bpp // 8
        data = self.vid[self.tex_offset:]
        for y in range(self.height):
            for x in range(self.width):
                a, r, g, b = self.decode(data, (xs[x] | ys[y]) * step)
                px[x, y] = (r, g, b, a)
        return img

    def save(self, path, img, glyphs=None, header_size=None):
        """This font with img as its texture (one level, sides powers of
        two): glyphs (else the glyph table as it is) and, with header_size,
        the texture size the header gives (version 4 pulls the coordinates
        in by a texel of it)."""
        w, h = img.width, img.height
        if w & (w - 1) or h & (h - 1) or w > 4096 or h > 4096:
            raise ValueError('the texture sides must be powers of two')
        xs, ys = morton_tables(w, h)
        step = self.bpp // 8
        data = bytearray(w * h * step)
        px = img.load()
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                i = (xs[x] | ys[y]) * step
                data[i:i + step] = self.encode(a, r, g, b)
        sys_ = bytearray(self.sys)
        u = w.bit_length() - 1
        v = h.bit_length() - 1
        fmt = (self.tex_format & ~(0xFF << 20) & ~(0xF << 16)) | 1 << 16 | u << 20 | v << 24
        struct.pack_into('<I', sys_, 0x0C, fmt)
        if glyphs is not None:
            for i, g in enumerate(glyphs):
                struct.pack_into('<ffffhhhh', sys_, self.glyphs_at + i * 0x18, *g)
        if header_size is not None:
            struct.pack_into('<II', sys_, 0x14 + 0x10, header_size[0], header_size[1])
        vid = bytes(self.vid[:self.tex_offset]) + bytes(data)
        total = self.header_size + len(vid)
        with open(path, 'wb') as out:
            out.write(struct.pack('<III', XPR0, total, self.header_size) + bytes(sys_) + vid)


# ---- Redrawing a font larger ----

def system_font(name):
    windir = os.environ.get('WINDIR', r'C:\Windows')
    for d in (os.path.join(windir, 'Fonts'), '/usr/share/fonts/truetype/msttcorefonts'):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


# Faces to try for a font file, by the start of its name.
GUESSES = {'arial': ['arial.ttf', 'arialbd.ttf', 'Arial.ttf', 'Arial_Bold.ttf']}


def split_layers(cell):
    """A glyph cell (RGBA) as two masks: the fill (light texels) and the
    shadow (dark ones)."""
    from PIL import Image
    fill = Image.new('L', cell.size)
    shadow = Image.new('L', cell.size)
    fp, sp, cp = fill.load(), shadow.load(), cell.load()
    for y in range(cell.height):
        for x in range(cell.width):
            r, g, b, a = cp[x, y]
            light = (r + g + b) / 765.0
            fp[x, y] = int(a * light + 0.5)
            sp[x, y] = int(a * (1 - light) + 0.5)
    return fill, shadow


def ink_box(mask, threshold=64):
    return mask.point(lambda v: 255 if v >= threshold else 0).getbbox()


def drawn_like(font, ch, want, size, scale=1):
    """ch drawn with font into an image of size, its ink centered where
    want (the old glyph's ink box, in old texels) is, times scale."""
    from PIL import Image, ImageDraw
    w, h = size
    big = Image.new('L', (w * 3, h * 3))
    ImageDraw.Draw(big).text((w, h), ch, fill=255, font=font)
    got = ink_box(big)
    if got is None:
        return None
    cx = (want[0] + want[2]) * 0.5 * scale
    cy = (want[1] + want[3]) * 0.5 * scale
    left = int(round((got[0] + got[2]) * 0.5 - cx))
    top = int(round((got[1] + got[3]) * 0.5 - cy))
    return big.crop((left, top, left + w, top + h))


def sample_cells(f, old, chars):
    """[(character, fill, shadow)] for those of chars the font has."""
    cells = []
    for ch in chars:
        c = ord(ch)
        if c <= f.max_glyph and f.table[c] != 0:
            fill, shadow = split_layers(old.crop(f.cell(f.table[c])))
            if ink_box(fill) is not None:
                cells.append((ch, fill, shadow))
    return cells


def find_shadow(cells):
    """The drop shadow's offset, or None: where the fill, moved, covers most
    of the dark texels."""
    from PIL import ImageChops
    shift = None
    if sum(sum(s.getdata()) for _, _, s in cells) > 255 * 8:
        best = -1
        for dx in range(4):
            for dy in range(4):
                if dx or dy:
                    score = sum(sum(ImageChops.multiply(ImageChops.offset(fill, dx, dy), shadow).getdata())
                                for _, fill, shadow in cells)
                    if score > best:
                        best, shift = score, (dx, dy)
    return shift


def fit(f, old, faces, chars):
    """((face, size), shadow offset or None, mean error): the TrueType face
    and size that draw the font's glyphs most alike, tried on a few
    characters."""
    from PIL import ImageChops, ImageFont, ImageStat
    cells = sample_cells(f, old, chars)
    shift = find_shadow(cells)
    best_face, best_err = None, None
    for path in faces:
        for tenth in range(int(f.cell_height * 3), int(f.cell_height * 11)):
            font = ImageFont.truetype(path, tenth / 10.0)
            err = 0.0
            for ch, fill, _ in cells:
                m = drawn_like(font, ch, ink_box(fill), fill.size)
                err += 255.0 if m is None else ImageStat.Stat(ImageChops.difference(m, fill)).mean[0]
            if best_err is None or err < best_err:
                best_face, best_err = (path, tenth / 10.0), err
    return best_face, shift, best_err / max(1, len(cells))


SUPER = 8


class TtfFace:
    """A TrueType face drawn SUPER times the old font's size."""

    def __init__(self, path, size):
        from PIL import ImageFont
        self.font = ImageFont.truetype(path, size * SUPER)

    def has(self, ch):
        return self.font.getmask(ch).getbbox() is not None

    def big(self, ch, stroke):
        from PIL import Image, ImageDraw
        em = int(self.font.size)
        img = Image.new('L', (em * 4, em * 3))
        ImageDraw.Draw(img).text((em, em // 2), ch, fill=255, font=self.font, stroke_width=stroke,
                                 stroke_fill=255)
        return img


class Xtf:
    """The Xbox dashboard's own font files (C:\\XBox Book.xtf, C:\\Xbox.xtf),
    version XTF0: each glyph a mesh of triangles in em units, y up. (The
    copies in C:\\fonts are XTF1, whose glyph data is compressed.)"""

    def __init__(self, path):
        d = open(path, 'rb').read()
        if d[:4] != b'XTF0':
            raise SystemExit('%s: %r; only XTF0 files are read (C:\\ has them; C:\\fonts has XTF1)'
                             % (path, d[:4]))
        self.d = d
        namelen, = struct.unpack_from('<I', d, 4)
        self.name = d[8:8 + namelen].split(b'\0')[0].decode('latin-1')
        at = 8 + namelen
        _, _, count, nranges = struct.unpack_from('<IIII', d, at)
        at += 16
        self.index = {}
        n = 0
        for i in range(nranges):
            low, cnt = struct.unpack_from('<HH', d, at + i * 4)
            for k in range(cnt):
                self.index[low + k] = n + k
            n += cnt
        self.records = at + nranges * 4
        self.count = count

    def mesh(self, ch):
        """([(x, y)], [(i, j, k)]) for ch, or None."""
        g = self.index.get(ord(ch))
        if g is None or g >= self.count:
            return None
        off, = struct.unpack_from('<I', self.d, self.records + g * 28 + 24)
        icount, vcount = struct.unpack_from('<HH', self.d, off)
        if icount == 0 or vcount == 0 or icount % 3:
            return None
        idx = struct.unpack_from('<%dH' % icount, self.d, off + 4)
        verts = struct.unpack_from('<%df' % (vcount * 2), self.d, off + 4 + icount * 2)
        pts = [(verts[i * 2], verts[i * 2 + 1]) for i in range(vcount)]
        return pts, [idx[i:i + 3] for i in range(0, icount, 3)]


class XtfFace:
    """An XTF font's glyphs drawn from their triangles, px pixels to the em
    (SUPER times the old size)."""

    def __init__(self, path, px):
        self.xtf = Xtf(path)
        self.px = px

    def has(self, ch):
        return self.xtf.mesh(ch) is not None

    def big(self, ch, stroke):
        from PIL import Image, ImageDraw, ImageFilter
        m = self.xtf.mesh(ch)
        pts, tris = m
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        margin = 4 + stroke
        w = int((max(xs) - min(xs)) * self.px) + margin * 2 + 1
        h = int((max(ys) - min(ys)) * self.px) + margin * 2 + 1
        img = Image.new('L', (w, h))
        draw = ImageDraw.Draw(img)
        ox, top = min(xs), max(ys)
        for t in tris:
            draw.polygon([((pts[i][0] - ox) * self.px + margin, (top - pts[i][1]) * self.px + margin) for i in t],
                         fill=255)
        if stroke:
            img = img.filter(ImageFilter.MaxFilter(stroke * 2 + 1))
        return img


def fitted(face, ch, want, size, scale, stroke):
    """ch (the face is SUPER times the old size) with strokes stroke wider,
    stretched onto want (the old glyph's ink box) times scale, in an image
    of size."""
    from PIL import Image
    big = face.big(ch, stroke)
    got = ink_box(big, 128)
    if got is None:
        return None
    tw = max(1, (want[2] - want[0]) * scale)
    th = max(1, (want[3] - want[1]) * scale)
    # (The box at full coverage is a little inside the drawn edge: keep the
    # half texel of fringe on each side.)
    pad = SUPER // 2
    glyph = big.crop((got[0] - pad, got[1] - pad, got[2] + pad, got[3] + pad))
    glyph = glyph.resize((tw + scale, th + scale), Image.LANCZOS)
    out = Image.new('L', size)
    out.paste(glyph, (want[0] * scale - scale // 2, want[1] * scale - scale // 2))
    return out


def fit_weight(face, glyphs):
    """The stroke (in SUPER-size pixels) that makes the glyphs, back at the
    old size, cover as much as the old ones."""
    old_ink = sum(sum(fill.tobytes()) for _, _, _, fill, want in glyphs if want)
    best, best_err = 0, None
    for stroke in range(0, SUPER + 1):
        ink = 0
        for _, c, box, fill, want in glyphs:
            if not want or not face.has(chr(c)):
                continue
            m = fitted(face, chr(c), want, fill.size, 1, stroke)
            if m is not None:
                ink += sum(m.tobytes())
        err = abs(ink / float(old_ink) - 1.0)
        if best_err is None or err < best_err:
            best, best_err = stroke, err
    return best


def hires(src, dst, scale, faces):
    from PIL import Image, ImageChops, ImageFont
    f = XprFont(open(src, 'rb').read())
    old = f.image()
    if not faces:
        stem = os.path.splitext(os.path.basename(src))[0].lower()
        for key, names in GUESSES.items():
            if stem.startswith(key):
                faces = [p for p in (system_font(n) for n in names) if p]
    if not faces:
        raise SystemExit('%s: no face for it (give one with --face)' % src)
    if faces[0].lower().endswith('.xtf'):
        # The Xbox's own font: the same design, so no search for the face;
        # the glyphs are fitted to the old ones' boxes below anyway.
        font = XtfFace(faces[0], f.cell_height * SUPER)
        shift = find_shadow(sample_cells(f, old, 'HOAaxgW0@%&ky'))
        print('%s: %s (XTF), shadow %s' % (os.path.basename(src), font.xtf.name, shift))
    else:
        (face, size), shift, err = fit(f, old, faces, 'HOAaxgW0@%&ky')
        print('%s: %s at %.1f px, shadow %s, fit error %.1f of 255' % (
            os.path.basename(src), os.path.basename(face), size, shift, err))
        font = TtfFace(face, size)
    # Each glyph is drawn large (SUPER times the old size), then fitted to
    # the old one's ink box, so it's as wide and tall as it was (the old
    # ones were drawn small with hinting, which makes bold strokes wider
    # than the outlines), and the strokes thickened as much as makes the
    # font as heavy as it was.
    glyphs = []
    for g, c in sorted(f.characters().items()):
        if g >= f.num_glyphs:
            continue
        box = f.cell(g)
        if box[2] <= box[0] or box[3] <= box[1]:
            continue
        fill, _ = split_layers(old.crop(box))
        glyphs.append((g, c, box, fill, ink_box(fill, 128)))
    stroke = fit_weight(font, [x for x in glyphs if chr(x[1]) in 'HOAaxgW0ekmns@%&'])
    print('  strokes thickened %.2f px' % (stroke / float(SUPER)))
    # (What isn't redrawn: the old glyphs, enlarged.)
    new = old.resize((f.width * scale, f.height * scale), Image.LANCZOS)
    redrawn = kept = 0
    for g, c, box, fill, want in glyphs:
        size2 = ((box[2] - box[0]) * scale, (box[3] - box[1]) * scale)
        m = None
        if want is not None and c > 0x20 and font.has(chr(c)):
            m = fitted(font, chr(c), want, size2, scale, stroke)
        if m is None:
            kept += 1
            continue
        cell = Image.new('RGBA', size2, (0, 0, 0, 0))
        if shift is not None:
            sm = ImageChops.offset(m, shift[0] * scale, shift[1] * scale)
            black = sm.point(lambda v: 0)
            cell.alpha_composite(Image.merge('RGBA', (black, black, black, sm)))
        white = m.point(lambda v: 255)
        cell.alpha_composite(Image.merge('RGBA', (white, white, white, m)))
        new.paste(cell, (box[0] * scale, box[1] * scale))
        redrawn += 1
    f.save(dst, new)
    print('  %d glyphs redrawn, %d enlarged as they were; %dx%d texture, %d bytes' % (
        redrawn, kept, new.width, new.height, os.path.getsize(dst)))
    return f, old, new


def square(src, dst, scale=1, img=None):
    """The font with its texture made square (padded, the glyphs where they
    were), and scale times larger. Some emulators (xemu 0.8) draw a font
    whose texture is taller than it is wide (XboxDings.xpr, 256x512) as
    stripes. img is a texture to use, already scale times larger."""
    from PIL import Image
    f = XprFont(open(src, 'rb').read())
    big = img if img is not None else f.image()
    if img is None and scale != 1:
        big = big.resize((f.width * scale, f.height * scale), Image.LANCZOS)
    side = max(f.width, f.height) * scale
    canvas = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    canvas.paste(big, (0, 0))
    sx = float(f.width * scale) / side
    sy = float(f.height * scale) / side
    glyphs = [(g[0] * sx, g[1] * sy, g[2] * sx, g[3] * sy) + tuple(g[4:]) for g in f.glyphs]
    one = side // scale   # (the header's size is in the font's own texels)
    f.save(dst, canvas, glyphs, (one, one) if f.version == 4 else None)
    print('%s: %dx%d texture, %d bytes' % (os.path.basename(dst), side, side, os.path.getsize(dst)))


def info(path):
    f = XprFont(open(path, 'rb').read())
    print('%s: version %d, texture %dx%d %s (%d levels), cell %.1f, line %.1f, %d glyphs, chars up to U+%04X' % (
        os.path.basename(path), f.version, f.width, f.height, FORMATS[f.fmt][0], f.levels, f.cell_height,
        f.line_height, f.num_glyphs, f.max_glyph))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('info', help='describe fonts')
    a.add_argument('xpr', nargs='+')
    a = sub.add_parser('png', help='the font texture as a .png')
    a.add_argument('xpr')
    a.add_argument('png')
    a = sub.add_parser('hires', help='the font with a larger, redrawn texture')
    a.add_argument('xpr')
    a.add_argument('out')
    a.add_argument('--scale', type=int, default=2, choices=(2, 4))
    a.add_argument('--face', action='append', default=[], help='a TrueType file to draw with (repeatable)')
    a = sub.add_parser('square', help='the font with a square texture (and --scale times larger)')
    a.add_argument('xpr')
    a.add_argument('out')
    a.add_argument('--scale', type=int, default=1, choices=(1, 2, 4))
    args = p.parse_args()
    if args.cmd == 'info':
        for x in args.xpr:
            try:
                info(x)
            except ValueError as e:
                print('%s: %s' % (x, e))
    elif args.cmd == 'png':
        XprFont(open(args.xpr, 'rb').read()).image().save(args.png)
    elif args.cmd == 'square':
        square(args.xpr, args.out, args.scale)
    else:
        hires(args.xpr, args.out, args.scale, args.face)


if __name__ == '__main__':
    main()
