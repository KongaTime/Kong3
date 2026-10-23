#!/usr/bin/env python3
"""PS5 RPCS3 - the launcher's fonts and logo, written into ps5/assets/ (kept in the repository).

- fonts/Inter-<weight>.ttf: static instances of Inter (SIL OFL 1.1, no reserved
  name) cut from its variable font at Google Fonts' pinned commit, subset to Latin;
  RPCS3's overlays draw with stb_truetype, which reads only a font's default instance.
- launcher/rpcs3-logo.png: the mark and the title's name, Kong³, drawn at three times the
  overlays' 1280x720 space (the console's 3840x2160). The letters are drawn with
  Orbitron Medium (OFL 1.1); the font itself is not shipped.
- launcher/mark.png: the mark alone and large, for the intro's white screen. The
  mark is kongatime's crowned 3 (ps5/art/icon-source.webp, the title's icon) as
  a rounded tile.
- launcher/trash.png, launcher/patch.png: the Delete and Patches buttons' icons
  (--icons-only writes just these, without the network).

Needs fontTools and Pillow. Run once; commit what it writes.
"""

import hashlib
import io
import math
import sys
import urllib.request
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "ps5" / "assets"

FONTS_COMMIT = "7085eb89a950e85db5b166b7a58d414544b4140c"
FILES = {
    "inter": ("ofl/inter/Inter%5Bopsz,wght%5D.ttf", "29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031"),
    "inter-ofl": ("ofl/inter/OFL.txt", "5b9321a4298cfeb6b34354164a1c3afc3db114569984c502b9b35d988fd58c57"),
    "orbitron": ("ofl/orbitron/Orbitron%5Bwght%5D.ttf", "f42db2dd16e642258e35782916eceb1dcdbea06fb958d77ad71dc5963587e8fd"),
}

# name: (weight, optical size)
WEIGHTS = {
    "Regular": (400, 14),
    "Medium": (500, 14),
    "SemiBold": (600, 14),
    "Bold": (700, 28),
}

# Latin-1, Latin Extended-A, punctuation, and the signs games put in their names
UNICODES = list(range(0x20, 0x7f)) + list(range(0xa0, 0x180)) + list(range(0x2010, 0x2028)) + \
    [0x2030, 0x2039, 0x203a, 0x20ac, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2715]

SCALE = 3  # the console's display over the overlays' virtual space


def fetch(key):
    path, sha256 = FILES[key]
    url = f"https://raw.githubusercontent.com/google/fonts/{FONTS_COMMIT}/{path}"
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != sha256:
        sys.exit(f"{url}: unexpected sha256")
    return data


def write_fonts(inter):
    folder = ASSETS / "fonts"
    folder.mkdir(parents=True, exist_ok=True)
    for name, (weight, opsz) in WEIGHTS.items():
        font = instancer.instantiateVariableFont(TTFont(io.BytesIO(inter)), {"wght": weight, "opsz": opsz})
        options = subset.Options()
        options.layout_features = ["kern", "liga"]
        options.name_IDs = ["*"]
        options.notdef_outline = True
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=UNICODES)
        subsetter.subset(font)
        target = folder / f"Inter-{name}.ttf"
        font.save(target)
        print(f"wrote {target.relative_to(ROOT)} ({target.stat().st_size} bytes)")
    (ROOT / "ps5" / "licenses" / "OFL-1.1-Inter.txt").write_bytes(fetch("inter-ofl"))


def draw_mark(size):
    """kongatime's crowned 3 (the title's icon) as a rounded tile, with a faint rim."""
    ss = 4  # supersampled, then reduced
    n = size * ss
    art = Image.open(ROOT / "ps5" / "art" / "icon-source.webp").convert("RGBA").resize((n, n), Image.LANCZOS)
    radius = n * 0.22
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, n - 1, n - 1], radius=radius, fill=255)
    tile = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    tile.paste(art, (0, 0), mask)
    rim = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    w = max(2, round(n * 0.018))
    ImageDraw.Draw(rim).rounded_rectangle([w / 2, w / 2, n - 1 - w / 2, n - 1 - w / 2], radius=radius - w / 2,
                                          outline=(255, 255, 255, 56), width=w)
    tile.alpha_composite(rim)
    return tile.resize((size, size), Image.LANCZOS)


def write_mark():
    """The mark alone, 150 virtual pixels square, for the intro's white screen."""
    image = draw_mark(150 * SCALE)
    target = ASSETS / "launcher" / "mark.png"
    image.save(target, optimize=True)
    print(f"wrote {target.relative_to(ROOT)} ({image.width}x{image.height})")


def write_logo(orbitron):
    # Virtual layout: a 28-pixel mark, a 10-pixel gap, letters 12 pixels high
    cube = 28 * SCALE
    gap = 10 * SCALE
    cap = 12 * SCALE

    font = TTFont(io.BytesIO(orbitron))
    font = instancer.instantiateVariableFont(font, {"wght": 500})
    buffer = io.BytesIO()
    font.save(buffer)
    cap_em = font["OS/2"].sCapHeight / font["head"].unitsPerEm
    pil = ImageFont.truetype(io.BytesIO(buffer.getvalue()), round(cap / cap_em))

    # Kong, then a superscript 3 (the title's name, Kong³): drawn as a smaller 3
    # with its top on the capitals' line, as Orbitron has no superscripts
    text = "Kong"
    tracking = round(1.2 * SCALE)
    widths = [pil.getlength(c) for c in text]
    superscript = ImageFont.truetype(io.BytesIO(buffer.getvalue()), round(cap * 0.6 / cap_em))
    sup_w = superscript.getlength("3")
    text_w = math.ceil(sum(widths) + tracking * len(text) + sup_w)
    width = cube + gap + text_w + SCALE
    image = Image.new("RGBA", (width, cube), (0, 0, 0, 0))
    image.alpha_composite(draw_mark(cube), (0, 0))

    d = ImageDraw.Draw(image)
    x = cube + gap
    baseline = (cube + cap) / 2
    for c, w in zip(text, widths):
        d.text((x, baseline), c, font=pil, fill=(255, 255, 255, 255), anchor="ls")
        x += w + tracking
    d.text((x, baseline - cap + cap * 0.6), "3", font=superscript, fill=(255, 255, 255, 255), anchor="ls")

    folder = ASSETS / "launcher"
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / "rpcs3-logo.png"
    image.save(target, optimize=True)
    print(f"wrote {target.relative_to(ROOT)} ({image.width}x{image.height}, {image.width // SCALE}x{image.height // SCALE} virtual)")


def write_trash():
    """The Delete button's bin, white, at three times its 20-pixel size."""
    ss = 8
    n = 20 * SCALE * ss
    image = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    white = (255, 255, 255, 255)
    u = n / 20  # one virtual pixel
    w = round(1.6 * u)
    # Lid, handle, body with rounded foot, and three slats
    d.rounded_rectangle([3 * u, 4.2 * u, 17 * u, 4.2 * u + w], radius=w / 2, fill=white)
    d.rounded_rectangle([7.5 * u, 1.8 * u, 12.5 * u, 4.6 * u], radius=1.2 * u, outline=white, width=w)
    d.rounded_rectangle([4.6 * u, 6.4 * u, 15.4 * u, 18.4 * u], radius=2 * u, outline=white, width=w)
    for x in (8.2, 11.8):
        d.rounded_rectangle([x * u - w / 2, 9 * u, x * u + w / 2, 15.6 * u], radius=w / 2, fill=white)
    image = image.resize((n // ss, n // ss), Image.LANCZOS)
    target = ASSETS / "launcher" / "trash.png"
    image.save(target, optimize=True)
    print(f"wrote {target.relative_to(ROOT)}")


def write_patch():
    """The Patches button's plaster, white, at three times its 20-pixel size: a
    strip with a pad in the middle and holes at its ends, laid across the corner."""
    ss = 8
    n = 20 * SCALE * ss
    u = n / 20  # one virtual pixel
    w = round(1.6 * u)
    white = (255, 255, 255, 255)
    # Drawn level, on a square the strip's length across, then turned
    big = round(n * 1.5)
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    cx = cy = big / 2
    half_l, half_h = 9.4 * u, 3.8 * u
    d.rounded_rectangle([cx - half_l, cy - half_h, cx + half_l, cy + half_h], radius=half_h, outline=white, width=w)
    pad = 2.7 * u
    d.rounded_rectangle([cx - pad, cy - half_h + w / 2, cx + pad, cy + half_h - w / 2], radius=0.8 * u, fill=white)
    r = 0.62 * u
    for side in (-1, 1):
        for dx, dy in ((5.0, -1.2), (5.0, 1.2), (6.9, 0)):
            x, y = cx + side * dx * u, cy + dy * u
            d.ellipse([x - r, y - r, x + r, y + r], fill=white)
    image = image.rotate(45, resample=Image.BICUBIC)
    left = (big - n) // 2
    image = image.crop((left, left, left + n, left + n)).resize((n // ss, n // ss), Image.LANCZOS)
    target = ASSETS / "launcher" / "patch.png"
    image.save(target, optimize=True)
    print(f"wrote {target.relative_to(ROOT)}")


def main():
    if "--icons-only" in sys.argv:
        write_trash()
        write_patch()
        return
    if "--logo-only" not in sys.argv:
        write_fonts(fetch("inter"))
        write_trash()
        write_patch()
    write_logo(fetch("orbitron"))
    write_mark()


if __name__ == "__main__":
    main()
