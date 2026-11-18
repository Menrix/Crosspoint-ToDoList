#pragma once
// Post-it cover for lists (added by the Crosspoint-ToDoList patch).
//
// CrossPoint uses an image named "cover.bmp" next to a text file as its cover. The list screen
// writes /Lists/cover.bmp once, so every list shows as a sticky note on the home screen, and the
// home screen no longer tries (and fails) to find a cover every time.
//
// Pure C++17, no device code: builds the BMP file in memory (8-bit grayscale, ~120 KB).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace todolist {

constexpr int kCoverW = 300;
constexpr int kCoverH = 400;

namespace cover {

inline int clamp8(const int v) { return std::max(0, std::min(255, v)); }

struct Canvas {
  std::vector<uint8_t> px = std::vector<uint8_t>(static_cast<size_t>(kCoverW) * kCoverH, 255);
  void set(const int x, const int y, const int v) {
    if (x >= 0 && y >= 0 && x < kCoverW && y < kCoverH) px[static_cast<size_t>(y) * kCoverW + x] = static_cast<uint8_t>(clamp8(v));
  }
  int get(const int x, const int y) const {
    if (x < 0 || y < 0 || x >= kCoverW || y >= kCoverH) return 255;
    return px[static_cast<size_t>(y) * kCoverW + x];
  }
  void darken(const int x, const int y, const int v) { set(x, y, std::min(get(x, y), v)); }
  void stroke(const int x0, const int y0, const int x1, const int y1, const int thick, const int v) {
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1;
    for (int i = 0; i <= steps; i++) {
      const int x = x0 + (x1 - x0) * i / steps;
      const int y = y0 + (y1 - y0) * i / steps;
      for (int dy = -thick / 2; dy <= thick / 2; dy++)
        for (int dx = -thick / 2; dx <= thick / 2; dx++) darken(x + dx, y + dy, v);
    }
  }
};

}  // namespace cover

// The finished BMP file.
inline std::vector<uint8_t> makeCoverBmp() {
  using cover::Canvas;
  Canvas c;
  // The note: a square, a soft shadow to the lower right, a curled lower-right corner.
  const int nx = 30;
  const int ny = 90;
  const int ns = 240;
  for (int y = 0; y < ns + 14; y++) {
    for (int x = 0; x < ns + 14; x++) {
      const int sx = x - 8;
      const int sy = y - 10;
      const int out = std::max({0, sx - ns, sy - ns});
      if (sx >= -4 && sy >= -4) c.darken(nx + x, ny + y, 150 + out * 15);  // shadow
    }
  }
  for (int y = 0; y < ns; y++) {
    for (int x = 0; x < ns; x++) {
      const int fromCorner = (ns - x) + (ns - y);
      if (fromCorner < 46) continue;                  // cut corner (curled up)
      int v = 222 - (y * 10) / ns;                    // a faint gradient like light on paper
      if (fromCorner < 60) v -= (60 - fromCorner);    // shading towards the curl
      c.set(nx + x, ny + y, v);
    }
  }
  // the curl itself: a lighter triangle folded back over the corner
  for (int y = 0; y < 46; y++) {
    for (int x = 0; x < 46; x++) {
      if (x + y < 46 && x + y > 46 - 46) {
        const int px = nx + ns - 46 + y;
        const int py = ny + ns - 46 + x;
        if (x + y >= 44) {
          c.set(px, py, 120);
        } else if (y + x < 44) {
          c.set(nx + ns - 1 - x, ny + ns - 1 - y, 255);  // background shows where the corner lifted
          c.set(px, py, 238 - (x + y));
        }
      }
    }
  }
  c.stroke(nx + ns - 46, ny + ns, nx + ns, ny + ns - 46, 2, 120);
  // tape strip across the top, slightly crooked, with torn ends
  for (int y = 0; y < 36; y++) {
    for (int x = 0; x < 120; x++) {
      const int tx = nx + ns / 2 - 60 + x;
      const int ty = ny - 18 + y + x / 20;
      const bool edge = (x < 3 + (y * 7) % 4) || (x > 116 - (y * 5) % 4);
      if (edge) continue;
      c.set(tx, ty, std::min(c.get(tx, ty) + 25, 246));
    }
  }
  // three checklist rows in pencil: a box (first one ticked) and a wavy line
  for (int row = 0; row < 3; row++) {
    const int by = ny + 60 + row * 52;
    const int bx = nx + 28;
    const int b = 24;
    c.stroke(bx, by, bx + b, by + 1, 3, 40);
    c.stroke(bx + b, by + 1, bx + b - 1, by + b, 3, 40);
    c.stroke(bx + b - 1, by + b, bx + 1, by + b - 1, 3, 40);
    c.stroke(bx + 1, by + b - 1, bx, by, 3, 40);
    if (row == 0) {
      c.stroke(bx - 3, by - 2, bx + b + 3, by + b + 2, 4, 20);
      c.stroke(bx + b + 3, by - 3, bx - 2, by + b + 3, 4, 20);
    }
    const int len = 130 - row * 25;
    for (int i = 0; i < len; i++) {
      const int x = bx + b + 18 + i;
      const int y = by + b / 2 + static_cast<int>(3.0 * std::sin(i * 0.25 + row));
      c.darken(x, y, 60);
      c.darken(x, y + 1, 60);
      c.darken(x, y + 2, 90);
    }
  }

  // BMP: 8-bit, grayscale palette, rows bottom-up, padded to 4 bytes.
  const int rowBytes = (kCoverW + 3) & ~3;
  const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes) * kCoverH;
  const uint32_t offset = 14 + 40 + 256 * 4;
  const uint32_t fileSize = offset + pixelBytes;
  std::vector<uint8_t> bmp;
  bmp.reserve(fileSize);
  auto u16 = [&](const uint32_t v) {
    bmp.push_back(v & 0xFF);
    bmp.push_back((v >> 8) & 0xFF);
  };
  auto u32 = [&](const uint32_t v) {
    u16(v & 0xFFFF);
    u16(v >> 16);
  };
  bmp.push_back('B');
  bmp.push_back('M');
  u32(fileSize);
  u32(0);
  u32(offset);
  u32(40);
  u32(kCoverW);
  u32(kCoverH);
  u16(1);
  u16(8);
  u32(0);
  u32(pixelBytes);
  u32(2835);
  u32(2835);
  u32(256);
  u32(0);
  for (int i = 0; i < 256; i++) {
    bmp.push_back(static_cast<uint8_t>(i));
    bmp.push_back(static_cast<uint8_t>(i));
    bmp.push_back(static_cast<uint8_t>(i));
    bmp.push_back(0);
  }
  for (int y = kCoverH - 1; y >= 0; y--) {
    for (int x = 0; x < kCoverW; x++) bmp.push_back(c.px[static_cast<size_t>(y) * kCoverW + x]);
    for (int p = kCoverW; p < rowBytes; p++) bmp.push_back(0);
  }
  return bmp;
}

}  // namespace todolist
