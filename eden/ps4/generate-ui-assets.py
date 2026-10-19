#!/usr/bin/env python3
"""Bake the launcher's fonts and icons into ps4_ui_assets.h.

Text: Nunito Regular (SIL Open Font License), Latin glyphs, at the sizes the HOME menu uses
(1920x1080 layout). Icons: drawn here from simple shapes (4x supersampled), not taken from any
console firmware. Everything is an 8-bit alpha mask; the launcher tints it at run time.
"""
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT_SIZES = [20, 22, 24, 26, 28, 30, 32, 36, 40]
CODEPOINTS = (list(range(32, 127)) + list(range(160, 256)) +
              [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2122])
SS = 4  # supersampling for icons


def bake_font(path, size):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    glyphs = []
    for cp in CODEPOINTS:
        ch = chr(cp)
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
        advance = int(round(font.getlength(ch)))
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        img = Image.new('L', (max(1, w), max(1, h)))
        if w and h:
            ImageDraw.Draw(img).text((-x0, -y0), ch, fill=255, font=font, anchor='ls')
        glyphs.append((cp, img, w, h, x0, y0, advance))
    # Shelf packing into a 1024-wide atlas.
    atlas_w, x, y, shelf = 1024, 0, 0, 0
    placed = []
    for cp, img, w, h, x0, y0, adv in glyphs:
        if x + w + 1 > atlas_w:
            x, y, shelf = 0, y + shelf + 1, 0
        placed.append((cp, x, y, w, h, x0, y0, adv, img))
        x += w + 1
        shelf = max(shelf, h)
    atlas_h = y + shelf + 1
    atlas = Image.new('L', (atlas_w, atlas_h))
    for cp, gx, gy, w, h, x0, y0, adv, img in placed:
        if w and h:
            atlas.paste(img, (gx, gy))
    return size, ascent, descent, atlas, placed


# ---- icons --------------------------------------------------------------------------------------

def canvas(w, h):
    img = Image.new('L', (w * SS, h * SS))
    return img, ImageDraw.Draw(img)


def done(img, w, h):
    return img.resize((w, h), Image.Resampling.LANCZOS)


def S(v):
    return v * SS


def ring(d, cx, cy, r, width):
    d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], outline=255, width=int(S(width)))


def disc(d, cx, cy, r, fill=255):
    d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], fill=fill)


def line(d, pts, width, fill=255):
    d.line([(S(x), S(y)) for x, y in pts], fill=fill, width=int(S(width)), joint='curve')
    for x, y in (pts[0], pts[-1]):
        disc(d, x, y, width / 2, fill)


def rrect(d, x0, y0, x1, y1, r, fill=None, outline=None, width=1):
    d.rounded_rectangle([S(x0), S(y0), S(x1), S(y1)], radius=S(r), fill=fill, outline=outline,
                        width=int(S(width)) if outline is not None else 0)


def icon_info(n):
    img, d = canvas(n, n)
    c = n / 2
    ring(d, c, c, n * 0.40, n * 0.075)
    disc(d, c, c - n * 0.17, n * 0.055)
    rrect(d, c - n * 0.045, c - n * 0.07, c + n * 0.045, c + n * 0.24, n * 0.04, fill=255)
    return done(img, n, n)


def icon_folder(n):
    img, d = canvas(n, n)
    w = n * 0.07
    pts = [(n * .14, n * .30), (n * .40, n * .30), (n * .47, n * .38), (n * .86, n * .38), (n * .86, n * .76),
           (n * .14, n * .76), (n * .14, n * .30)]
    d.line([(S(x), S(y)) for x, y in pts], fill=255, width=int(S(w)), joint='curve')
    line(d, [(n * .14, n * .48), (n * .86, n * .48)], w)
    return done(img, n, n)


def icon_users(n):
    img, d = canvas(n, n)
    w = n * .06
    for cx, cy, r, sh in ((n * .5, n * .33, n * .10, n * .20), (n * .25, n * .40, n * .08, n * .15),
                          (n * .75, n * .40, n * .08, n * .15)):
        ring(d, cx, cy, r, w)
        d.arc([S(cx - sh), S(cy + r + n * .05), S(cx + sh), S(cy + r + n * .05 + sh * 2)], 180, 360,
              fill=255, width=int(S(w)))
    return done(img, n, n)


def icon_controller(n):
    # A single tilted joystick controller: rounded body, a stick and four buttons.
    big = Image.new('L', (S(n), S(n)))
    d = ImageDraw.Draw(big)
    w = n * .065
    rrect(d, n * .36, n * .10, n * .64, n * .90, n * .14, outline=255, width=w)
    ring(d, n * .5, n * .32, n * .07, w)
    for dx, dy in ((0, -.07), (0, .07), (-.07, 0), (.07, 0)):
        disc(d, n * (.5 + dx), n * (.62 + dy), n * .03)
    big = big.rotate(-45, resample=Image.Resampling.BICUBIC)
    return big.resize((n, n), Image.Resampling.LANCZOS)


def icon_gear(n):
    img, d = canvas(n, n)
    c = n / 2
    teeth = 8
    for i in range(teeth):
        a = i * 2 * math.pi / teeth
        ux, uy = math.cos(a), math.sin(a)
        px, py = -uy, ux
        r0, r1, half = n * .26, n * .43, n * .065
        pts = [(c + ux * r0 + px * half, c + uy * r0 + py * half), (c + ux * r1 + px * half, c + uy * r1 + py * half),
               (c + ux * r1 - px * half, c + uy * r1 - py * half), (c + ux * r0 - px * half, c + uy * r0 - py * half)]
        d.polygon([(S(x), S(y)) for x, y in pts], fill=255)
    disc(d, c, c, n * .31)
    disc(d, c, c, n * .14, 0)
    return done(img, n, n)


def icon_power(n):
    img, d = canvas(n, n)
    c = n / 2
    w = n * .07
    r = n * .30
    d.arc([S(c - r), S(c - r), S(c + r), S(c + r)], -55, 235, fill=255, width=int(S(w)))
    for a in (-55, 235):
        disc(d, c + r * math.cos(math.radians(a)) - 0, c + r * math.sin(math.radians(a)), w / 2)
    line(d, [(c, n * .14), (c, n * .48)], w)
    return done(img, n, n)


def icon_grid(n):
    img, d = canvas(n, n)
    w = n * .07
    for gx in (0, 1):
        for gy in (0, 1):
            x0, y0 = n * (.14 + gx * .40), n * (.14 + gy * .40)
            rrect(d, x0, y0, x0 + n * .32, y0 + n * .32, n * .03, outline=255, width=w)
    return done(img, n, n)


def icon_pad(w, h):
    # Footer controller: body with two grips, d-pad and buttons cut out.
    img, d = canvas(w, h)
    rrect(d, w * .10, h * .12, w * .90, h * .62, h * .25, fill=255)
    disc(d, w * .24, h * .60, h * .26)
    disc(d, w * .76, h * .60, h * .26)
    cx, cy, a = w * .27, h * .38, h * .10
    d.rectangle([S(cx - a * 1.6), S(cy - a * .5), S(cx + a * 1.6), S(cy + a * .5)], fill=0)
    d.rectangle([S(cx - a * .5), S(cy - a * 1.6), S(cx + a * .5), S(cy + a * 1.6)], fill=0)
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        disc(d, w * .73 + dx * h * .12, h * .38 + dy * h * .12, h * .055, 0)
    return done(img, w, h)


def button_glyph(n, kind):
    """A filled disc with the PS face symbol cut out (drawn in one color, background shows through)."""
    img, d = canvas(n, n)
    c = n / 2
    disc(d, c, c, n * .48)
    w = n * .10
    if kind == 'cross':
        k = n * .17
        line(d, [(c - k, c - k), (c + k, c + k)], w, 0)
        line(d, [(c - k, c + k), (c + k, c - k)], w, 0)
    elif kind == 'circle':
        d.ellipse([S(c - n * .2), S(c - n * .2), S(c + n * .2), S(c + n * .2)], outline=0, width=int(S(w)))
    elif kind == 'square':
        k = n * .18
        d.rectangle([S(c - k), S(c - k), S(c + k), S(c + k)], outline=0, width=int(S(w)))
    elif kind == 'triangle':
        k = n * .22
        pts = [(c, c - k), (c + k * 1.05, c + k * .75), (c - k * 1.05, c + k * .75), (c, c - k)]
        d.line([(S(x), S(y)) for x, y in pts], fill=0, width=int(S(w)), joint='curve')
    elif kind == 'options':
        for j in (-1, 0, 1):
            line(d, [(c - n * .18, c + j * n * .14), (c + n * .18, c + j * n * .14)], n * .075, 0)
    elif kind == 'l1' or kind == 'r1':
        pass
    return done(img, n, n)


def icon_chevron(n, direction):
    img, d = canvas(n, n)
    w = n * .12
    if direction == 'down':
        line(d, [(n * .25, n * .38), (n * .5, n * .62), (n * .75, n * .38)], w)
    elif direction == 'left':
        line(d, [(n * .62, n * .25), (n * .38, n * .5), (n * .62, n * .75)], w)
    else:
        line(d, [(n * .38, n * .25), (n * .62, n * .5), (n * .38, n * .75)], w)
    return done(img, n, n)


def icon_check(n):
    img, d = canvas(n, n)
    line(d, [(n * .18, n * .52), (n * .40, n * .74), (n * .82, n * .28)], n * .12)
    return done(img, n, n)


def icon_plus(n):
    img, d = canvas(n, n)
    w = n * .09
    line(d, [(n * .5, n * .2), (n * .5, n * .8)], w)
    line(d, [(n * .2, n * .5), (n * .8, n * .5)], w)
    return done(img, n, n)


def icon_person(n):
    img, d = canvas(n, n)
    disc(d, n * .5, n * .36, n * .17)
    d.ellipse([S(n * .20), S(n * .58), S(n * .80), S(n * 1.10)], fill=255)
    return done(img, n, n)


def icon_backspace(n):
    img, d = canvas(n, n)
    w = n * .07
    pts = [(n * .32, n * .25), (n * .86, n * .25), (n * .86, n * .75), (n * .32, n * .75), (n * .12, n * .5),
           (n * .32, n * .25)]
    d.line([(S(x), S(y)) for x, y in pts], fill=255, width=int(S(w)), joint='curve')
    line(d, [(n * .48, n * .38), (n * .70, n * .62)], w)
    line(d, [(n * .48, n * .62), (n * .70, n * .38)], w)
    return done(img, n, n)


def icon_shift(n):
    img, d = canvas(n, n)
    w = n * .07
    pts = [(n * .5, n * .16), (n * .84, n * .50), (n * .66, n * .50), (n * .66, n * .82), (n * .34, n * .82),
           (n * .34, n * .50), (n * .16, n * .50), (n * .5, n * .16)]
    d.line([(S(x), S(y)) for x, y in pts], fill=255, width=int(S(w)), joint='curve')
    return done(img, n, n)


# Avatar symbols: simple original shapes drawn white on the user's colour.
def avatar_symbol(n, kind):
    img, d = canvas(n, n)
    c = n / 2
    if kind == 'star':
        pts = []
        for i in range(10):
            r = n * (.40 if i % 2 == 0 else .17)
            a = -math.pi / 2 + i * math.pi / 5
            pts.append((S(c + r * math.cos(a)), S(c + r * math.sin(a))))
        d.polygon(pts, fill=255)
    elif kind == 'heart':
        disc(d, c - n * .14, c - n * .08, n * .17)
        disc(d, c + n * .14, c - n * .08, n * .17)
        d.polygon([(S(c - n * .30), S(c)), (S(c + n * .30), S(c)), (S(c), S(c + n * .33))], fill=255)
        d.polygon([(S(c - n * .305), S(c - n * .06)), (S(c + n * .305), S(c - n * .06)), (S(c), S(c + n * .33))],
                  fill=255)
    elif kind == 'moon':
        disc(d, c, c, n * .33)
        disc(d, c + n * .15, c - n * .10, n * .28, 0)
    elif kind == 'sun':
        disc(d, c, c, n * .18)
        for i in range(8):
            a = i * math.pi / 4
            line(d, [(c + math.cos(a) * n * .27, c + math.sin(a) * n * .27),
                     (c + math.cos(a) * n * .38, c + math.sin(a) * n * .38)], n * .06)
    elif kind == 'note':
        disc(d, c - n * .10, c + n * .20, n * .11)
        line(d, [(c - n * .01, c + n * .20), (c - n * .01, c - n * .30)], n * .06)
        line(d, [(c - n * .01, c - n * .30), (c + n * .22, c - n * .20)], n * .07)
    elif kind == 'bolt':
        pts = [(c + n * .08, c - n * .38), (c - n * .20, c + n * .05), (c - n * .01, c + n * .05),
               (c - n * .10, c + n * .38), (c + n * .21, c - n * .07), (c + n * .02, c - n * .07)]
        d.polygon([(S(x), S(y)) for x, y in pts], fill=255)
    elif kind == 'flower':
        for i in range(5):
            a = -math.pi / 2 + i * 2 * math.pi / 5
            disc(d, c + math.cos(a) * n * .17, c + math.sin(a) * n * .17, n * .13)
        disc(d, c, c, n * .10, 0)
    elif kind == 'crown':
        pts = [(c - n * .32, c + n * .22), (c - n * .32, c - n * .18), (c - n * .14, c), (c, c - n * .26),
               (c + n * .14, c), (c + n * .32, c - n * .18), (c + n * .32, c + n * .22)]
        d.polygon([(S(x), S(y)) for x, y in pts], fill=255)
    elif kind == 'diamond':
        pts = [(c, c - n * .36), (c + n * .27, c), (c, c + n * .36), (c - n * .27, c)]
        d.polygon([(S(x), S(y)) for x, y in pts], fill=255)
    elif kind == 'cloud':
        disc(d, c - n * .15, c + n * .05, n * .15)
        disc(d, c + n * .03, c - n * .06, n * .20)
        disc(d, c + n * .19, c + n * .07, n * .14)
        d.rectangle([S(c - n * .15), S(c + n * .05), S(c + n * .19), S(c + n * .21)], fill=255)
    elif kind == 'smile':
        ring(d, c, c, n * .33, n * .06)
        disc(d, c - n * .11, c - n * .08, n * .045)
        disc(d, c + n * .11, c - n * .08, n * .045)
        d.arc([S(c - n * .17), S(c - n * .12), S(c + n * .17), S(c + n * .19)], 20, 160, fill=255,
              width=int(S(n * .06)))
    elif kind == 'person':
        return icon_person(n)
    return done(img, n, n)


def shadow_square(n, blur):
    """Blurred square for 9-slice shadows: n x n with the square inset by `blur`*2."""
    img = Image.new('L', (n, n))
    d = ImageDraw.Draw(img)
    d.rectangle([blur * 2, blur * 2, n - blur * 2 - 1, n - blur * 2 - 1], fill=255)
    return img.filter(ImageFilter.GaussianBlur(blur))


def shadow_circle(n, blur):
    img = Image.new('L', (n, n))
    d = ImageDraw.Draw(img)
    d.ellipse([blur * 2, blur * 2, n - blur * 2 - 1, n - blur * 2 - 1], fill=255)
    return img.filter(ImageFilter.GaussianBlur(blur))


AVATARS = ['person', 'star', 'heart', 'moon', 'sun', 'note', 'bolt', 'flower', 'crown', 'diamond', 'cloud', 'smile']


def masks():
    m = []
    for name, f in (('info', icon_info), ('folder', icon_folder), ('users', icon_users),
                    ('controller', icon_controller), ('gear', icon_gear), ('power', icon_power)):
        m.append((name, f(84)))
        m.append((name + '_72', f(72)))
        m.append((name + '_small', f(60)))
    m.append(('grid', icon_grid(108)))
    m.append(('grid_small', icon_grid(60)))
    m.append(('pad', icon_pad(51, 36)))
    m.append(('pad_big', icon_pad(160, 112)))
    for kind in ('cross', 'circle', 'square', 'triangle', 'options'):
        m.append(('btn_' + kind, button_glyph(36, kind)))
    for direction in ('down', 'left', 'right'):
        m.append(('chevron_' + direction, icon_chevron(32, direction)))
    m.append(('check', icon_check(36)))
    m.append(('plus', icon_plus(72)))
    m.append(('backspace', icon_backspace(44)))
    m.append(('shift', icon_shift(44)))
    for kind in AVATARS:
        m.append(('avatar_' + kind, avatar_symbol(192, kind)))
    m.append(('shadow_square', shadow_square(48, 6)))
    m.append(('shadow_circle', shadow_circle(164, 7)))
    return m


def c_bytes(data):
    out = []
    for i in range(0, len(data), 32):
        out.append(','.join(str(b) for b in data[i:i + 32]))
    return ',\n'.join(out)


def main():
    font_path, out_path = sys.argv[1], Path(sys.argv[2])
    lines = ['// Generated by ps4/generate-ui-assets.py. Do not edit.', '#pragma once', '#include <cstdint>',
             'namespace UiAssets {',
             'struct Glyph { uint32_t cp; uint16_t x, y; uint8_t w, h; int8_t left; int8_t top; uint8_t advance; };',
             'struct Font { int size; int ascent; int descent; int atlas_w; int atlas_h; const uint8_t* atlas;'
             ' int count; const Glyph* glyphs; };',
             'struct Mask { const char* name; int w; int h; const uint8_t* alpha; };']
    fonts = []
    for size in FONT_SIZES:
        sz, ascent, descent, atlas, placed = bake_font(font_path, size)
        lines.append(f'inline const uint8_t font{size}_atlas[] = {{\n{c_bytes(atlas.tobytes())}}};')
        glyph_rows = [f'{{{cp},{x},{y},{w},{h},{x0},{y0},{adv}}}' for cp, x, y, w, h, x0, y0, adv, _ in placed]
        lines.append(f'inline const Glyph font{size}_glyphs[] = {{' + ','.join(glyph_rows) + '};')
        fonts.append(f'{{{size},{ascent},{descent},{atlas.width},{atlas.height},font{size}_atlas,'
                     f'{len(placed)},font{size}_glyphs}}')
    lines.append('inline const Font fonts[] = {' + ','.join(fonts) + '};')
    lines.append(f'constexpr int font_count = {len(fonts)};')
    mask_rows = []
    for name, img in masks():
        lines.append(f'inline const uint8_t mask_{name}[] = {{\n{c_bytes(img.tobytes())}}};')
        mask_rows.append(f'{{"{name}",{img.width},{img.height},mask_{name}}}')
    lines.append('inline const Mask masks[] = {' + ','.join(mask_rows) + '};')
    lines.append(f'constexpr int mask_count = {len(mask_rows)};')
    lines.append(f'constexpr int avatar_symbol_count = {len(AVATARS)};')
    lines.append('}  // namespace UiAssets')
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text('\n'.join(lines) + '\n')
    if len(sys.argv) > 3:  # preview sheet for checking the icons
        sheet = Image.new('L', (1200, 700), 45)
        x = y = 10
        row = 0
        for name, img in masks():
            if x + img.width > 1190:
                x, y = 10, y + row + 10
                row = 0
            sheet.paste(Image.new('L', img.size, 235), (x, y), img)
            x += img.width + 10
            row = max(row, img.height)
        sheet.save(sys.argv[3])


if __name__ == '__main__':
    main()
