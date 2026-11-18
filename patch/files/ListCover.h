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

#include "ListPaper.h"

namespace todolist {

constexpr int kCoverW = 300;
constexpr int kCoverH = 450;  // 2:3, like a book cover

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

// The finished BMP file: a small notepad page in book-cover proportions (2:3), torn from the
// spiral like the list pages, with a few ticked and open boxes and pencil scribbles.
inline std::vector<uint8_t> makeCoverBmp() {
  using cover::Canvas;
  Canvas c;
  const auto px = [&](const int x, const int y, const bool black) { c.set(x, y, black ? 0 : 255); };
  // Same drawing as a list page, scaled to the cover. No rounded corners: the home screen
  // draws its own frame around every cover.
  const auto g = listpaper::geometry(kCoverW, kCoverH, 40, 6, 6, 6, 0);
  listpaper::paintPaper(g, 0x5EED1234u, px, false);
  // headline: a thick felt-pen scribble
  for (int i = 0; i < (g.right - g.textX) * 2 / 3; i++) {
    const int x = g.textX + i;
    const int y = g.firstRuleY - 16 + static_cast<int>(4.0 * std::sin(i * 0.21));
    for (int t = -3; t <= 3; t++) c.darken(x, y + t, 0);
  }
  // items: boxes (the first two ticked) and pencil scribbles of different length
  const int rows = std::min(g.lineCount, 7);
  for (int i = 0; i < rows; i++) {
    listpaper::paintBox(g, i, i < 2, i + 3, px);
    const int len = (g.right - g.textX) * (55 + (i * 37) % 40) / 100;
    const int base = listpaper::ruleY(g, i) - 12;
    for (int k = 0; k < len; k++) {
      const int y = base + static_cast<int>(3.5 * std::sin(k * 0.3 + i));
      c.darken(g.textX + k, y, 30);
      c.darken(g.textX + k, y + 1, 30);
      c.darken(g.textX + k, y + 2, 60);
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
