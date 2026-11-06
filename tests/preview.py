#!/usr/bin/env python3
"""Draw roughly what the calculator viewer shows for a page.

    ./test_core PAGE.bin > layout.txt
    python3 preview.py layout.txt SPRITE_DIR out.png [first_line]

SPRITE_DIR holds the NAME.bin sprite files (convert with html2ce.py --bin).
The font is only an approximation of the calculator's; positions, sizes, colours
and image placement follow main.c.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont



def build_palette():  # same fixed palette as html2ce.py and main.c
    pal = [(r * 51, g * 51, b * 51) for r in range(6) for g in range(6) for b in range(6)]
    return pal + [(((k + 1) * 255) // 40,) * 3 for k in range(40)]


PAL = build_palette()
C_BLACK, C_WHITE, C_LINK, C_ITAL, C_DEAD = 0, 255, 4, 108, 235
BAR_H, VIEW_TOP = 12, 14
VIEW_BOT = 240 - BAR_H
VIEW_W, MARGIN = 312, 4
SCALE = {0: (1, 1), 1: (2, 2), 2: (1, 2), 3: (1, 1)}

FONT = None
for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
             "C:/Windows/Fonts/consolab.ttf"):
    try:
        FONT = ImageFont.truetype(cand, 10)
        break
    except OSError:
        pass
FONT = FONT or ImageFont.load_default()


def glyph(ch, color, sx, sy, bold):
    cell = Image.new("L", (8, 8), 0)
    ImageDraw.Draw(cell).text((0, -1), ch, font=FONT, fill=255)
    if sx != 1 or sy != 1:
        cell = cell.resize((8 * sx, 8 * sy), Image.NEAREST)
    return cell


def parse(path):
    doc, lines = {}, []
    for raw in Path(path).read_text().splitlines():
        if raw.startswith("DOC"):
            doc["head"] = raw
        elif raw.startswith("L "):
            f = dict(kv.split("=") for kv in raw.split()[2:])
            lines.append({"i": int(raw.split()[1]), **{k: int(v) for k, v in f.items()},
                          "segs": [], "img": None})
        elif raw.startswith("S "):
            _, st, ln, *txt = raw.split(" ", 3)
            lines[-1]["segs"].append((int(st), (txt[0] if txt else "")[:int(ln)].ljust(int(ln))))
        elif raw.startswith("I "):
            _, name, w, h = raw.split()
            lines[-1]["img"] = (name, int(w), int(h))
        elif raw.startswith("ERR"):
            print(raw)
    return doc, lines


def main():
    layout, sprites, out = sys.argv[1:4]
    top = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    doc, lines = parse(layout)
    img = Image.new("RGB", (320, 240), PAL[C_WHITE])
    d = ImageDraw.Draw(img)
    y0 = lines[top]["y"] if lines else 0
    for ln in lines[top:]:
        y = VIEW_TOP + ln["y"] - y0
        if y >= VIEW_BOT:
            break
        if ln["kind"] == 2:  # image
            name, w, h = ln["img"]
            data = (Path(sprites) / (name + ".bin")).read_bytes()
            sp = Image.new("RGB", (w, h))
            sp.putdata([PAL[b] for b in data[2:]])
            x = MARGIN + ln["indent"]
            if ln["center"] and w < VIEW_W - ln["indent"]:
                x += (VIEW_W - ln["indent"] - w) // 2
            img.paste(sp, (x, y + 2))
        elif y + ln["h"] <= VIEW_BOT:
            if ln["kind"] == 1:
                d.line((MARGIN + ln["indent"], y + 3, MARGIN + VIEW_W - 1, y + 3), fill=PAL[C_DEAD])
                continue
            width = sum(len(t) * 8 * SCALE[(st >> 2) & 3][0] for st, t in ln["segs"])
            x = MARGIN + ln["indent"]
            if ln["center"] and width < VIEW_W - ln["indent"]:
                x += (VIEW_W - ln["indent"] - width) // 2
            for st, text in ln["segs"]:
                head = (st >> 2) & 3
                sx, sy = SCALE[head]
                bold = bool(st & 1) or head != 0
                col = (C_LINK if st & 0x10 else C_DEAD if st & 0x20 else C_ITAL if st & 2 else C_BLACK)
                x0 = x
                for ch in text:
                    if ch != " ":
                        g = glyph(ch, col, sx, sy, bold)
                        gy = y + ln["h"] - 1 - 8 * sy
                        img.paste(PAL[col], (x, gy), g)
                        if bold:
                            img.paste(PAL[col], (x + 1, gy), g)
                    x += 8 * sx
                if st & 0x30:
                    d.line((x0, y + ln["h"] - 1, x - 1, y + ln["h"] - 1), fill=PAL[col])
    for yy in (0, VIEW_BOT):
        d.rectangle((0, yy, 319, yy + BAR_H - 1), fill=PAL[C_BLACK])
    d.text((4, 1), doc.get("head", "").split('"')[1] if '"' in doc.get("head", "") else "", fill=(255, 255, 255), font=FONT)
    d.text((4, VIEW_BOT + 1), "line %d/%d" % (top + 1, len(lines)), fill=(255, 255, 255), font=FONT)
    img.resize((640, 480), Image.NEAREST).save(out)
    print("wrote", out)


main()
