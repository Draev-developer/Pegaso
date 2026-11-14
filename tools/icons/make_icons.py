"""Pegaso artwork generator.

Draws the Pegasus head emblem and writes the application icon, the tray icons and the
logo images into srchybrid/res. Everything is drawn from the coordinates below, so the
artwork can be tuned and regenerated:

    python tools/icons/make_icons.py [--preview out.png]

Requires Pillow.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, 'srchybrid', 'res')

# Palettes: background gradient (inner, outer), rim
BLUE = dict(inner=(86, 142, 255), outer=(22, 38, 122), rim=(12, 22, 84))
GREY = dict(inner=(170, 174, 184), outer=(84, 88, 100), rim=(52, 54, 62))
AMBER = dict(inner=(255, 196, 84), outer=(196, 104, 12), rim=(128, 62, 6))

WHITE = (255, 255, 255, 255)
SHADE = (205, 222, 250, 255)   # shadowed white
GOLD = (255, 190, 60, 255)
GOLD_DARK = (214, 138, 22, 255)
INK = (16, 24, 70, 255)

# All shapes live in a 100 x 100 box, y pointing down, the horse looking right.
HEAD = [(61, 21), (67, 25), (77, 39), (87, 54), (93, 62), (94, 69), (90, 76), (81, 77), (70, 73), (62, 69),
        (58, 80), (61, 99), (31, 99), (31, 80), (34, 60), (40, 42), (47, 28), (51, 22)]
EAR = [(48, 27), (56, 3), (65, 25)]
EAR_INNER = [(53, 23), (56.5, 10), (61, 23)]
MANE = [(56, 21), (47, 22), (38, 25), (30, 32), (35, 38), (24, 44), (31, 51), (20, 58), (28, 65), (18, 74),
        (27, 81), (19, 99), (40, 99), (40, 80), (42, 62), (47, 46), (52, 34), (57, 26)]
FORELOCK = [(56, 19), (65, 22), (71, 31), (72, 37), (66, 31), (60, 27)]
WING_TIPS = [(6, 6), (1, 22), (1, 38), (4, 53), (10, 67), (18, 79)]   # feather tips, top to bottom
WING_ROOT = (36, 84)
WING_TOP = (36, 40)
EYE = (73, 43)
NOSTRIL = (89.5, 65.5)


def chaikin(points, rounds=2):
    """Corner cutting: turns a coarse closed polygon into a smooth outline."""
    for _ in range(rounds):
        out = []
        for i, p in enumerate(points):
            q = points[(i + 1) % len(points)]
            out.append((0.75 * p[0] + 0.25 * q[0], 0.75 * p[1] + 0.25 * q[1]))
            out.append((0.25 * p[0] + 0.75 * q[0], 0.25 * p[1] + 0.75 * q[1]))
        points = out
    return points


def feather(root_top, root_bottom, tip, bulge=0.16):
    """A feather: a leaf shape from two root points to a tip."""
    mid = ((root_top[0] + root_bottom[0]) / 2, (root_top[1] + root_bottom[1]) / 2)
    dx, dy = tip[0] - mid[0], tip[1] - mid[1]
    nx, ny = -dy * bulge, dx * bulge
    half = (mid[0] + dx * 0.55, mid[1] + dy * 0.55)
    return chaikin([root_top, (half[0] - nx, half[1] - ny), tip, (half[0] + nx, half[1] + ny), root_bottom], 2)


def emblem(size, palette=BLUE, detail=None, background=True):
    """Renders the emblem as a square RGBA image."""
    if detail is None:
        detail = size >= 40
    ss = 4 if size >= 128 else 8
    n = size * ss
    k = n / 100.0
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def sc(points, grow=0.0, center=(55, 55)):
        out = []
        for x, y in points:
            if grow:
                vx, vy = x - center[0], y - center[1]
                ln = math.hypot(vx, vy) or 1
                x, y = x + vx / ln * grow, y + vy / ln * grow
            out.append((x * k, y * k))
        return out

    if background:
        # disc with a radial gradient, lit from the top left
        grad = Image.new('RGBA', (n, n), (0, 0, 0, 0))
        gd = ImageDraw.Draw(grad)
        steps = 48
        for i in range(steps):
            t = i / (steps - 1)
            col = tuple(int(palette['outer'][c] + (palette['inner'][c] - palette['outer'][c]) * t) for c in range(3))
            r = (1 - t) * 70 + 4
            cx, cy = 38, 32
            gd.ellipse(((cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k), fill=col + (255,))
        mask = Image.new('L', (n, n), 0)
        ImageDraw.Draw(mask).ellipse((1 * k, 1 * k, 99 * k, 99 * k), fill=255)
        img.paste(grad, (0, 0), mask)
        rim = max(1.0, 2.2 if detail else 3.0)
        d.ellipse((1 * k, 1 * k, 99 * k, 99 * k), outline=palette['rim'] + (255,), width=int(rim * k))

    # everything else is clipped to the disc
    art = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    a = ImageDraw.Draw(art)
    outline = INK if detail else palette['rim'] + (255,)
    ow = 1.3 if detail else 2.6   # outline width in units

    # wing, behind the neck
    roots = [(WING_TOP[0] + (WING_ROOT[0] - WING_TOP[0]) * i / len(WING_TIPS),
              WING_TOP[1] + (WING_ROOT[1] - WING_TOP[1]) * i / len(WING_TIPS)) for i in range(len(WING_TIPS) + 1)]
    feathers = [feather(roots[i], roots[i + 1], WING_TIPS[i]) for i in range(len(WING_TIPS))]
    for f in feathers:
        a.polygon(sc(f, ow, center=(30, 55)), fill=outline)
    for i, f in enumerate(feathers):
        a.polygon(sc(f), fill=WHITE if i % 2 == 0 or not detail else SHADE)
    if detail:
        for f in feathers:
            a.line(sc(f) + sc(f)[:1], fill=(150, 176, 226, 255), width=max(1, int(0.5 * k)))

    head = chaikin(HEAD, 3)
    mane = chaikin(MANE, 2)
    # dark outline first, then the fills
    for shape in (head, mane, EAR):
        a.polygon(sc(shape, ow), fill=outline)
    a.polygon(sc(mane), fill=GOLD)
    if detail:
        a.polygon(sc(chaikin([(48, 25), (41, 30), (39, 40), (35, 50), (33, 62), (31, 78), (31, 99), (40, 99), (40, 80),
                              (42, 62), (47, 46), (52, 34)], 2)), fill=GOLD_DARK)
    a.polygon(sc(head), fill=WHITE)
    a.polygon(sc(EAR), fill=WHITE)
    if detail:
        a.polygon(sc(EAR_INNER), fill=(240, 176, 190, 255))
    a.polygon(sc(chaikin(FORELOCK, 2), ow * 0.6), fill=outline)
    a.polygon(sc(chaikin(FORELOCK, 2)), fill=GOLD)

    # eye and nostril
    er = 3.2 if detail else 4.6
    a.ellipse(((EYE[0] - er) * k, (EYE[1] - er * 1.15) * k, (EYE[0] + er) * k, (EYE[1] + er * 1.15) * k), fill=INK)
    if detail:
        a.ellipse(((EYE[0] - 0.3) * k, (EYE[1] - 2.6) * k, (EYE[0] + 1.7) * k, (EYE[1] - 0.6) * k), fill=WHITE)
        a.ellipse(((NOSTRIL[0] - 1.5) * k, (NOSTRIL[1] - 2.0) * k, (NOSTRIL[0] + 1.5) * k, (NOSTRIL[1] + 2.0) * k), fill=INK)
        a.line(sc([(83, 72.5), (88, 72), (91, 70)]), fill=INK, width=max(1, int(0.9 * k)))

    if background:
        clip = Image.new('L', (n, n), 0)
        ImageDraw.Draw(clip).ellipse((3 * k, 3 * k, 97 * k, 97 * k), fill=255)
        img.paste(art, (0, 0), Image.composite(art.split()[3], Image.new('L', (n, n), 0), clip))
    else:
        img = art
    return img.resize((size, size), Image.LANCZOS)


def save_ico(path, sizes, palette=BLUE):
    images = [emblem(s, palette) for s in sizes]
    images[-1].save(path, format='ICO', sizes=[(s, s) for s in sizes], append_images=images[:-1], bitmap_format='bmp')


UPSTREAM = 'c27f42f'   # commit with the unmodified eMule artwork

# Toolbar icons which carry a small mule: (file, corner the mule sits in, badge size relative to the icon)
BADGED = [('Kad.ico', 'br', 0.62), ('Server.ico', 'br', 0.62), ('Search.ico', 'bl', 0.62), ('Messages.ico', 'bl', 0.62),
          ('IRC.ico', 'bl', 0.62), ('Help.ico', 'bl', 0.62), ('emuleCollectionFileType.ico', 'bl', 0.72)]


def original_icon(name):
    """The eMule version of an icon, taken from the repository history so that reruns do not stack badges."""
    import io
    import subprocess
    data = subprocess.run(['git', '-C', ROOT, 'show', '%s:srchybrid/res/%s' % (UPSTREAM, name)], capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(data))


def badge_ico(name, corner, scale):
    """Covers the mule of a toolbar icon with the Pegasus emblem."""
    src = original_icon(name)
    sizes = sorted({sz[0] for sz in src.info.get('sizes', [src.size])})
    frames = []
    for size in sizes:
        try:
            base = src.ico.getimage((size, size), 32).convert('RGBA')
        except Exception:
            base = src.ico.getimage((size, size)).convert('RGBA')
        b = max(10, int(round(size * scale)))
        em = emblem(b)
        x = 0 if corner[1] == 'l' else size - b
        y = size - b if corner[0] == 'b' else 0
        # clear what is under the badge so nothing of the old figure shows around it
        hole = Image.new('L', (size, size), 255)
        top = 0 if name.startswith('emuleCollection') else y   # that mule reaches the top edge
        ImageDraw.Draw(hole).rectangle((x - 1 if x else 0, top, x + b if x else b, size), fill=0)
        base.putalpha(Image.composite(base.split()[3], Image.new('L', (size, size), 0), hole))
        base.paste(em, (x, y), em)
        frames.append(base)
    frames[-1].save(os.path.join(RES, name), format='ICO', sizes=[f.size for f in frames], append_images=frames[:-1], bitmap_format='bmp')


def font(size, bold=True):
    for name in (('segoeuib.ttf', 'arialbd.ttf') if bold else ('segoeui.ttf', 'arial.ttf')):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def vertical_gradient(size, top, bottom):
    img = Image.new('RGB', size)
    d = ImageDraw.Draw(img)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        d.line((0, y, size[0], y), fill=tuple(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3)))
    return img


def logo(size=(350, 300)):
    """Splash screen and About box image."""
    img = vertical_gradient(size, (14, 24, 86), (46, 96, 214)).convert('RGBA')
    # soft glow behind the emblem
    glow = Image.new('RGBA', size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((size[0] // 2 - 120, 8, size[0] // 2 + 120, 248), fill=(150, 200, 255, 110))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(28)))
    em = emblem(176)
    img.paste(em, ((size[0] - 176) // 2, 22), em)
    d = ImageDraw.Draw(img)
    title = font(46)
    w = d.textlength('Pegaso', font=title)
    d.text(((size[0] - w) / 2 + 2, 204), 'Pegaso', font=title, fill=(8, 14, 60, 255))
    d.text(((size[0] - w) / 2, 202), 'Pegaso', font=title, fill=(255, 255, 255, 255))
    sub = font(14, bold=False)
    text = 'eD2K  ·  Kad  ·  BitTorrent  ·  Web'
    w = d.textlength(text, font=sub)
    d.text(((size[0] - w) / 2, 262), text, font=sub, fill=(214, 228, 255, 255))
    return img.convert('RGB')


def wizard(size=(164, 314)):
    img = vertical_gradient(size, (255, 255, 255), (222, 234, 255)).convert('RGBA')
    em = emblem(140)
    img.paste(em, ((size[0] - 140) // 2, 60), em)
    d = ImageDraw.Draw(img)
    f = font(26)
    w = d.textlength('Pegaso', font=f)
    d.text(((size[0] - w) / 2, 214), 'Pegaso', font=f, fill=(22, 38, 122, 255))
    return img.convert('RGB')


def mini_background(size=(100, 121), base=(255, 208, 160)):
    """Watermark behind the mini window: keeps the original background colour."""
    img = Image.new('RGBA', size, base + (255,))
    em = emblem(92, background=False)
    faded = Image.new('RGBA', em.size, (255, 255, 255, 0))
    faded.paste(Image.new('RGBA', em.size, (255, 236, 214, 255)), (0, 0), em.split()[3].point(lambda v: int(v * 0.75)))
    img.paste(faded, (4, 16), faded)
    return img.convert('RGB')


def save_gif(img, path):
    img.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE).save(path, format='GIF')


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '--preview':
        sheet = Image.new('RGB', (1180, 560), (228, 228, 228))
        x = 10
        for s in (256, 64, 48, 32, 24, 16):
            em = emblem(s)
            sheet.paste(em, (x, 10), em)
            x += s + 12
        x = 10
        for pal in (BLUE, GREY, AMBER):
            for s in (16, 32):
                em = emblem(s, pal)
                big = em.resize((s * 4, s * 4), Image.NEAREST)
                sheet.paste(big, (x, 290), big)
                x += s * 4 + 10
        sheet.paste(logo(), (700, 10))
        sheet.paste(wizard().resize((123, 236)), (1056, 10))
        sheet.paste(mini_background(), (1060, 300))
        sheet.save(sys.argv[2])
        return

    save_ico(os.path.join(RES, 'Mule_Vista.ico'), [16, 24, 32, 48, 64, 256])
    save_ico(os.path.join(RES, 'TrayConnected.ico'), [16, 32], BLUE)
    save_ico(os.path.join(RES, 'TrayDisconnected.ico'), [16, 32], GREY)
    save_ico(os.path.join(RES, 'TrayLowID.ico'), [16, 32], AMBER)
    save_ico(os.path.join(RES, 'Mule.ico'), [16, 32, 48])
    for name, corner, scale in BADGED:
        badge_ico(name, corner, scale)
    logo().save(os.path.join(RES, 'logo.jpg'), quality=92)
    save_gif(wizard(), os.path.join(RES, 'mule_wiz.gif'))
    header = Image.alpha_composite(Image.new('RGBA', (49, 49), (255, 255, 255, 255)), emblem(49))
    save_gif(header.convert('RGB'), os.path.join(RES, 'mule_wiz_hdr.gif'))
    save_gif(mini_background(), os.path.join(RES, 'MiniMuleBack.gif'))
    print('artwork written to', RES)


if __name__ == '__main__':
    main()
