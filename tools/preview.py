#!/usr/bin/env python3
"""PC preview of a list page: notepad paper from ListPaper.h plus the handwriting text.
Usage: python3 preview.py out.png [--select N]   (needs Pillow, the font and a built preview_paper)"""
import subprocess, sys
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[2] if len(sys.argv) > 2 else "PatrickHand-Regular.ttf"
OUT = sys.argv[1] if len(sys.argv) > 1 else "preview.png"
W, H = 480, 800
PPEM = round(20 * 150 / 72)          # fontconvert: 20 pt at 150 dpi
font = ImageFont.truetype(FONT, PPEM)
asc, desc = font.getmetrics()
line_h = asc + desc

title = "Einkauf".upper()
TITLE_PPEM = round(26 * 150 / 72)
tfont = ImageFont.truetype(FONT, TITLE_PPEM)
tasc, tdesc = tfont.getmetrics()
entries = [("Milch", True), ("Brot", False), ("Batterien AA, die großen", False),
           ("Käse", True), ("Kaffee", False), ("Äpfel für den Kuchen am Sonntag", False),
           ("Tee", False), ("Spülmittel", False), ("Zahnpasta", True)]

def wrap(text, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if font.getlength(t) <= width or not cur:
            cur = t
        else:
            lines.append(cur); cur = w
    lines.append(cur)
    return lines

geo = subprocess.run(["./preview_paper", str(W), str(H), str(line_h), "0"], capture_output=True, text=True).stdout.split()
textX, titleY, listTop, pitch, lineCount, right, width = map(int, geo)
flags, layout, line = [], [], 0
for text, done in entries:
    lines = wrap(text, right - textX)
    if line + len(lines) > lineCount: break
    flags.append("1" if done else "0"); layout.append((line, lines))
    flags += ["x"] * (len(lines) - 1)
    line += len(lines)
# boxes go on the first line of each entry: pass one flag per text line, 'x' = no box
flag_str = ""
for f in flags: flag_str += f
sel = sys.argv[3] if len(sys.argv) > 3 else "-1"
seed = sys.argv[4] if len(sys.argv) > 4 else "1"
subprocess.run(["./preview_paper", str(W), str(H), str(line_h), flag_str.replace("x", "2"), sel, seed], capture_output=True)
img = Image.open("paper.pbm").convert("L")
img = Image.eval(img, lambda v: v)  # PBM: 0 = black
txt = Image.new("L", (W, H), 255)
d = ImageDraw.Draw(txt)
for ox in (0, 1, 2):  # thicker strokes, like a felt pen
    d.text((textX + ox, titleY + pitch - 10 - tasc), title, font=tfont, fill=0)
info = f"{sum(1 for _, dn in entries if not dn)} von {len(entries)} offen"
small = ImageFont.truetype("DejaVuSans.ttf", 17) if False else ImageFont.load_default(size=17)
d.text((right - d.textlength(info, font=small), titleY + pitch - 30), info, font=small, fill=0)
for (first, lines) in layout:
    for k, l in enumerate(lines):
        rule = listTop + (first + k + 1) * pitch - 6
        d.text((textX, rule - 4 - asc), l, font=font, fill=0)
txt = txt.point(lambda v: 0 if v < 128 else 255)
out = Image.composite(txt, img, txt.point(lambda v: 255 if v == 0 else 0))
out.save(OUT)
print("saved", OUT, "line_h", line_h, "pitch", pitch, "lines", lineCount)
