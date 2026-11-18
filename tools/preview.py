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

title = (sys.argv[6] if len(sys.argv) > 6 else "Einkauf").upper()
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
hidden_mode = len(sys.argv) > 5 and sys.argv[5] == "hidden"
for text, done in entries:
    if hidden_mode and done: continue
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
avail = right - textX
if d.textlength(title, font=tfont) > avail:
    tfont = ImageFont.truetype(FONT, PPEM); tasc = font.getmetrics()[0]
    while d.textlength(title, font=tfont) > avail and len(title) > 2:
        title = title[:-2] + "…"
for ox in (0, 1, 2):  # thicker strokes, like a felt pen
    d.text((textX + ox, titleY + pitch - 10 - tasc), title, font=tfont, fill=0)
done_n = sum(1 for _, dn in entries if dn)
hidden = len(sys.argv) > 5 and sys.argv[5] == "hidden"
import math
total_n = len(entries)
small = ImageFont.truetype(FONT, round(11 * 150 / 72))
sasc, sdesc = small.getmetrics()
foot_base = 800 - 22                      # baseline of the footer text
# hand-drawn separator: a slightly wavy pen stroke across the page
ly = foot_base - sasc - 14
pts = [(x, ly + 0.8 * math.sin(x / 53.0) + 0.3 * math.sin(x / 17.0)) for x in range(textX - 14 - 16 - 32 - 12, right + 1, 3)]
d.line(pts, fill=0, width=3)
# eye centred on the middle of the lower-case letters
xb = small.getbbox('x')            # x-height: centre the eye on the lower-case letters
import math as _m
box = 32; boxX = textX - 14 - 16 - box   # same column as the boxes (marginX - 16 - boxSize)
ex = boxX + box // 2; ey = int(foot_base - sasc + (xb[1] + xb[3]) / 2)
if not hidden:   # open eye: almond outline + pupil
    up = [(ex + x, ey - 9 * _m.cos(x / 15 * _m.pi / 2)) for x in range(-15, 16)]
    dn = [(ex + x, ey + 8 * _m.cos(x / 15 * _m.pi / 2)) for x in range(-15, 16)]
    d.line(up, fill=0, width=3); d.line(dn, fill=0, width=3)
    d.ellipse((ex - 5, ey - 5, ex + 5, ey + 5), fill=0)
else:            # closed eye: the lid as a smile-shaped curve with short lashes
    lid = [(ex + x, ey + 4 * _m.cos(x / 15 * _m.pi / 2) - 2) for x in range(-15, 16)]
    d.line(lid, fill=0, width=3)
    for lx in (-9, 0, 9):
        y0 = ey + 4 * _m.cos(lx / 15 * _m.pi / 2) - 1
        d.line((ex + lx, y0, ex + lx * 1.25, y0 + 7), fill=0, width=2)
gesture = sys.argv[7] if len(sys.argv) > 7 else "swipe up"
d.text((textX, foot_base - sasc), f"{gesture} to {'show' if hidden else 'hide'} checked", font=small, fill=0)
pg = "1/2"
d.text((right - d.textlength(pg, font=small), foot_base - sasc), pg, font=small, fill=0)
for (first, lines) in layout:
    for k, l in enumerate(lines):
        rule = listTop + (first + k + 1) * pitch - 6
        d.text((textX, rule - 4 - asc), l, font=font, fill=0)
txt = txt.point(lambda v: 0 if v < 128 else 255)
out = Image.composite(txt, img, txt.point(lambda v: 255 if v == 0 else 0))
out.save(OUT)
print("saved", OUT, "line_h", line_h, "pitch", pitch, "lines", lineCount)
