// PC preview of the notepad page. Draws exactly what ListPaper.h draws on the reader and writes
// a PBM image plus the geometry, so tools/preview.py can add the handwriting text on top.
//   g++ -std=c++17 -I../patch/files preview_paper.cpp -o preview_paper
//   ./preview_paper 480 800 50 "1,0,0,1,0" > paper.txt   (width height lineHeight done-flags)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ListPaper.h"

int main(int argc, char** argv) {
  const int w = argc > 1 ? std::atoi(argv[1]) : 480;
  const int h = argc > 2 ? std::atoi(argv[2]) : 800;
  const int lineHeight = argc > 3 ? std::atoi(argv[3]) : 50;
  const std::string flags = argc > 4 ? argv[4] : "";
  const int selected = argc > 5 ? std::atoi(argv[5]) : -1;
  const unsigned seed = argc > 6 ? static_cast<unsigned>(std::atoi(argv[6])) : 1u;

  std::vector<unsigned char> img(static_cast<size_t>(w) * h, 0);
  auto px = [&](const int x, const int y, const bool black) {
    if (x >= 0 && y >= 0 && x < w && y < h) img[static_cast<size_t>(y) * w + x] = black ? 1 : 0;
  };
  const auto g = listpaper::geometry(w, h, lineHeight, 8, 8, 8, 0);
  listpaper::paintPaper(g, seed, px);
  int line = 0;
  for (size_t i = 0; i < flags.size(); i++) {
    if (line >= g.lineCount) break;
    if (flags[i] != '0' && flags[i] != '1') {  // continuation line of a longer entry: no box
      line++;
      continue;
    }
    listpaper::paintBox(g, line, flags[i] == '1', line + 1, px);
    if (line == selected) listpaper::paintSelection(g, line, 1, px);
    line++;
  }
  FILE* f = std::fopen("paper.pbm", "wb");
  std::fprintf(f, "P4\n%d %d\n", w, h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x += 8) {
      unsigned char b = 0;
      for (int k = 0; k < 8; k++)
        if (x + k < w && img[static_cast<size_t>(y) * w + x + k]) b |= 0x80 >> k;
      std::fputc(b, f);
    }
  }
  std::fclose(f);
  std::printf("%d %d %d %d %d %d %d\n", g.textX, g.titleY, g.listTop, g.pitch, g.lineCount, g.right, g.width);
  return 0;
}
