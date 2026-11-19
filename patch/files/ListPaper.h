#pragma once
// Notepad look for to-do lists (added by the Crosspoint-ToDoList patch).
//
// Pure C++17, no device code: everything is drawn through one callback, pixel(x, y, black). Gray is a fixed dot pattern, so the page stays pure black and white on e-ink and
// turns as fast as any other page. The same code draws the PC preview (tools/preview_paper.cpp).
// pixel(x, y, black): black = true sets ink, false sets paper (white).
//
//   +--------------------------------------+
//   |##)        (o)  top punch hole         |
//   |##)  Shopping list              3/5    |   name in handwriting
//   |##)  ....|..........................   |   ruled lines: light gray dots
//   |##)  [x] | Milk                        |   boxes sit in the margin (= tick strip)
//   |##)  [ ] | Bread                       |   margin line: dark gray
//   |##)  ....|..........................   |
//   left: torn-off spiral edge (black with paper tabs and holes)

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>

namespace listpaper {

struct Geometry {
  int width = 0;
  int height = 0;
  int paperLeft = 0;  // x where the paper starts (left of it: the torn spiral edge)
  int marginX = 0;    // x of the vertical margin line; everything left of it is the tick strip
  int boxX = 0;       // left of the check boxes
  int boxSize = 0;
  int textX = 0;      // entry text starts here
  int right = 0;      // text ends here
  int titleY = 0;     // top of the list name
  int firstRuleY = 0; // y of the first ruled line (the area between header and list)
  int listTop = 0;    // top of the first entry line
  int pitch = 0;      // distance between ruled lines = one text line
  int lineCount = 0;  // ruled lines that fit on the page
  int footerRuleY = 0; // pen line above the footer
  int footerBaseline = 0;  // baseline of the footer text
  int holeX = 0;      // top punch hole
  int holeY = 0;
  int holeR = 0;
};

// lineHeight: height of one handwriting text line. top/right/bottom/left: unusable screen edges.
inline Geometry geometry(const int width, const int height, const int lineHeight, const int top, const int right,
                         const int bottom, const int left) {
  Geometry g;
  g.width = width;
  g.height = height;
  g.paperLeft = left + std::max(12, width / 40);  // thin dark strip: less e-ink ghosting, more room
  g.marginX = std::max(g.paperLeft + 96, std::min(width * 23 / 100, left + 112));
  g.boxSize = std::clamp(lineHeight * 11 / 20, 18, 34);
  g.boxX = g.marginX - 16 - g.boxSize;
  g.textX = g.marginX + 14;
  g.right = width - right - 12;
  g.pitch = std::max(lineHeight + 6, lineHeight * 6 / 5);
  // Header row: list name right of the margin line, the punched hole left of it (in the margin
  // column), both on the same height, like the hole of a notepad sheet.
  g.holeR = std::max(14, width / 26);
  g.titleY = top + 14;
  g.firstRuleY = g.titleY + g.pitch;
  g.holeX = (g.paperLeft + g.marginX) / 2 + 6;
  g.holeY = g.firstRuleY - g.pitch / 2 - 2;
  g.listTop = g.firstRuleY + 4;
  // Footer: a pen line, then "hide/show  3/9 done  1/2" on one small line.
  g.footerBaseline = height - bottom - 16;
  g.footerRuleY = g.footerBaseline - std::max(26, lineHeight / 2) - 14;
  const int usable = g.footerRuleY - 6 - g.listTop;
  g.lineCount = std::max(1, usable / g.pitch);
  return g;
}

// Ruled line under text line i (0 = first list line).
inline int ruleY(const Geometry& g, const int i) { return g.listTop + (i + 1) * g.pitch - 6; }
inline int lineTop(const Geometry& g, const int i) { return g.listTop + i * g.pitch; }

// ---------------------------------------------------------------------------------------------
// Drawing helpers on top of pixel(x, y, black). Gray levels: 1 = dark gray, 2 = light gray.

template <typename Px>
inline void dot(Px&& px, const int x, const int y, const int level) {
  if (level <= 0) {
    px(x, y, true);
  } else if (level == 1) {
    if (((x + y) & 1) == 0) px(x, y, true);  // 50 %
  } else {
    if (((x + 2 * y) & 3) == 0) px(x, y, true);  // 25 %, also on single-pixel lines: reads as pencil
  }
}

template <typename Px>
inline void fillRect(Px&& px, const int x, const int y, const int w, const int h, const int level = 0) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) dot(px, xx, yy, level);
}

template <typename Px>
inline void fillCircle(Px&& px, const int cx, const int cy, const int r, const int level = 0) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) dot(px, cx + x, cy + y, level);
}

// Thick line with round ends (Bresenham + a disc at each step).
template <typename Px>
inline void line(Px&& px, int x0, int y0, const int x1, const int y1, const int thick) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  const int r = thick / 2;
  for (;;) {
    if (r <= 0) {
      px(x0, y0, true);
    } else {
      fillCircle(px, x0, y0, r);
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// Small fixed wobble so boxes and ticks look drawn by hand but never change between redraws.
inline int wobble(const int seed, const int n) { return ((seed * 7919 + n * 104729) >> 3) % 3 - 1; }

// ---------------------------------------------------------------------------------------------

// The empty page: torn spiral edge, punch hole, margin line, ruled lines.
// seed: which slots are torn open. The reader derives it from the list name and the page number,
// so each page always looks the same and every page and list tears differently.
template <typename Px>
inline void paintPaper(const Geometry& g, const uint32_t seed, Px&& px, const bool roundCorners = true) {
  // Torn-off spiral edge, like a sheet torn from a spiral notepad: dark background on the left,
  // a column of punched slots just inside the paper edge. In an irregular pattern, some slots are
  // torn open towards the edge (the slot runs into the dark), the others are still closed.
  fillRect(px, 0, 0, g.paperLeft, g.height);
  const int pitch = std::max(40, g.height / 16);
  const int slotW = std::max(22, g.width / 20);
  const int slotH = std::max(14, slotW * 5 / 8);
  const int slotX = g.paperLeft + 5;  // left end of a slot
  const int r = slotH / 2;
  int index = 0;
  for (int cy = pitch / 2; cy < g.height + slotH; cy += pitch, index++) {
    // the slot: a black rounded rectangle
    for (int y = -r; y <= r; y++) {
      const int inset = r - static_cast<int>(std::sqrt(static_cast<double>(r * r - y * y)));
      for (int x = slotX + inset; x <= slotX + slotW - inset; x++) px(x, cy + y, true);
    }
    // torn open towards the edge, or still closed with its paper bridge
    // splitmix-style hash of (seed, slot): looks random, but the same page keeps its tears
    uint64_t z = (static_cast<uint64_t>(seed) << 32) + static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    // how torn the whole sheet is also varies, but closed slots always stay in the majority:
    // between 1 in 5 and a bit under half are torn open
    const uint32_t openPercent = 20 + (seed % 26);
    const bool torn = (z % 100) < openPercent;
    if (torn) {
      // the whole bridge is gone: the slot opens to the edge at nearly its full height,
      // with a ragged paper edge above and below the opening
      for (int y = -r + 1; y <= r - 1; y++) {
        const int fiber = (std::abs(y) >= r - 3) ? ((cy * 7 + y * 13 + index) % 4) : 0;
        for (int x = g.paperLeft - 1 + fiber; x < slotX + r; x++) px(x, cy + y, true);
      }
    }
  }
  // Rounded corners on the right, like a real sheet of paper.
  const int cr = std::max(12, g.width / 24);
  for (int y = 0; roundCorners && y < cr; y++) {
    for (int x = 0; x < cr; x++) {
      const int dx = cr - x;
      const int dy = cr - y;
      if (dx * dx + dy * dy > cr * cr) {
        px(g.width - cr + (cr - 1 - x), y, true);                 // top right
        px(g.width - cr + (cr - 1 - x), g.height - 1 - y, true);  // bottom right
      }
    }
  }

  // Punch hole at the top.
  fillCircle(px, g.holeX, g.holeY, g.holeR);

  // Margin line (dark gray, 3 px), top to bottom like on a real sheet.
  fillRect(px, g.marginX - 1, 0, 3, g.height, 1);

  // Ruled lines (light gray) under every text line, and one under the list name.
  for (int x = g.paperLeft + 4; x < g.width; x++) {
    dot(px, x, g.firstRuleY, 1);
  }
  for (int i = 0; i < g.lineCount; i++) {
    const int y = ruleY(g, i);
    for (int x = g.paperLeft + 4; x < g.width; x++) dot(px, x, y, 2);
  }
}

// A hand-drawn box at text line i, ticked when done.
template <typename Px>
inline void paintBox(const Geometry& g, const int lineIndex, const bool done, const int seed, Px&& px) {
  const int s = g.boxSize;
  const int x = g.boxX;
  const int y = ruleY(g, lineIndex) - 6 - s;  // sits on the same ruled line as the text
  line(px, x + wobble(seed, 1), y + wobble(seed, 2), x + s + wobble(seed, 3), y + wobble(seed, 4), 3);
  line(px, x + s + wobble(seed, 3), y + wobble(seed, 4), x + s + wobble(seed, 5), y + s + wobble(seed, 6), 3);
  line(px, x + s + wobble(seed, 5), y + s + wobble(seed, 6), x + wobble(seed, 7), y + s + wobble(seed, 8), 3);
  line(px, x + wobble(seed, 7), y + s + wobble(seed, 8), x + wobble(seed, 1), y + wobble(seed, 2), 3);
  if (done) {
    // A pen X, a little larger than the box, like ticked in a hurry.
    const int o = std::max(3, s / 6);
    line(px, x - o + 1, y - o + 2, x + s + o, y + s + o - 1, 5);
    line(px, x + s + o - 2, y - o, x - o + 2, y + s + o, 5);
  }
}

// X3/X4 selection: a bracket in the strip left of the box, over all lines of the entry.
template <typename Px>
inline void paintSelection(const Geometry& g, const int firstLine, const int lines, Px&& px) {
  const int x = g.boxX - 12;  // between the spiral slots and the boxes
  const int y0 = lineTop(g, firstLine) + 4;
  const int y1 = lineTop(g, firstLine + lines) - 10;
  line(px, x + 8, y0, x, y0, 4);
  line(px, x, y0, x, y1, 4);
  line(px, x, y1, x + 8, y1, 4);
}

// Pen line above the footer: nearly straight, with a slight natural wobble.
template <typename Px>
inline void paintFooterRule(const Geometry& g, Px&& px) {
  for (int x = g.boxX - 12; x <= g.right; x++) {  // from above the eye to the right edge
    const double w = 0.8 * std::sin(x / 53.0) + 0.3 * std::sin(x / 17.0);
    const int y = g.footerRuleY + static_cast<int>(std::lround(w));
    px(x, y - 1, true);
    px(x, y, true);
    px(x, y + 1, true);
  }
}

// Eye icon centred on (cx, cy): open = checked lines are shown, closed = they are hidden.
template <typename Px>
inline void paintEye(const int cx, const int cy, const bool closed, Px&& px) {
  const double halfPi = 1.5707963267948966;
  constexpr int half = 16;  // half the eye's width
  // A lid curve: y offset from cy at x (cos shape, 0 at both corners).
  const auto curve = [&](const int x, const double depth) {
    return static_cast<int>(std::lround(depth * std::cos(x / static_cast<double>(half) * halfPi)));
  };
  // Five lashes along a lid, pointing away from a centre point (cx, cy + from).
  const auto lashes = [&](const double depth, const int from, const int length) {
    for (const int lx : {-12, -6, 0, 6, 12}) {
      const int y0 = cy + curve(lx, depth);
      const double vx = lx;
      const double vy = y0 - (cy + from);
      const double n = std::sqrt(vx * vx + vy * vy);
      line(px, cx + lx, y0, cx + lx + static_cast<int>(std::lround(vx / n * length)),
           y0 + static_cast<int>(std::lround(vy / n * length)), 2);
    }
  };
  if (!closed) {
    // open: almond outline, a big round pupil with a light spot, lashes on top
    int lastUp = cy;
    int lastDown = cy;
    for (int x = -half; x <= half; x++) {
      const int up = cy - curve(x, 9);  // a little flatter on top
      const int down = cy + curve(x, 7);
      if (x > -half) {
        line(px, cx + x - 1, lastUp, cx + x, up, 2);
        line(px, cx + x - 1, lastDown, cx + x, down, 2);
      }
      lastUp = up;
      lastDown = down;
    }
    fillCircle(px, cx, cy - 2, 7);
    for (int y = -1; y <= 1; y++) {  // the light spot
      for (int x = -1; x <= 1; x++) {
        if (x * x + y * y <= 1) px(cx + 3 + x, cy - 5 + y, false);
      }
    }
    lashes(-9, 14, 5);
  } else {
    // closed: the lid as a curve like a smile, lashes hanging down
    int last = cy;
    for (int x = -half; x <= half; x++) {
      const int y = cy - 3 + curve(x, 7);
      if (x > -half) line(px, cx + x - 1, last, cx + x, y, 2);
      last = y;
    }
    for (const int lx : {-12, -6, 0, 6, 12}) {
      const int y0 = cy - 3 + curve(lx, 7);
      const double vx = lx;
      const double vy = y0 - (cy - 18);
      const double n = std::sqrt(vx * vx + vy * vy);
      line(px, cx + lx, y0, cx + lx + static_cast<int>(std::lround(vx / n * 5)),
           y0 + static_cast<int>(std::lround(vy / n * 5)), 2);
    }
  }
}

}  // namespace listpaper
