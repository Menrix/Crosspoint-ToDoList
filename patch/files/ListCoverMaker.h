#pragma once
// Writes a list's own home-screen cover: the notepad page with the list name as its heading
// (added by the Crosspoint-ToDoList patch).
//
// The reader draws the page into its screen memory (nothing is shown), reads it back and saves
// it as a black-and-white PNG in /.crosspoint/todolist. CrossPoint then scales that PNG for every
// cover size on the home screen and in the library.

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <string>

#include "ListCover.h"

namespace todolist {
namespace covermaker {

constexpr int kW = 480;  // cover picture size (3:5, like every cover thumbnail)
constexpr int kH = 800;
constexpr int kRowBytes = kW / 8;

// PNG checksums.
inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    ready = true;
  }
  for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}

struct PngWriter {
  HalFile& file;
  bool ok = true;
  uint32_t crc = 0;
  uint32_t adlerA = 1;
  uint32_t adlerB = 0;

  void raw(const uint8_t* data, size_t len) {
    if (ok && file.write(data, len) != len) ok = false;
  }
  void be32(uint32_t v) {
    const uint8_t b[4] = {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
                          static_cast<uint8_t>(v)};
    raw(b, 4);
  }
  void chunkStart(uint32_t length, const char* type) {
    be32(length);
    crc = crc32Update(0xFFFFFFFFu, reinterpret_cast<const uint8_t*>(type), 4);
    raw(reinterpret_cast<const uint8_t*>(type), 4);
  }
  void chunkData(const uint8_t* data, size_t len) {
    crc = crc32Update(crc, data, len);
    raw(data, len);
  }
  void chunkEnd() { be32(crc ^ 0xFFFFFFFFu); }
  void adler(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      adlerA = (adlerA + data[i]) % 65521u;
      adlerB = (adlerB + adlerA) % 65521u;
    }
  }
};

// Draws the plain page (without heading) for cover variant `index` into the screen memory.
inline void drawBase(const GfxRenderer& renderer, const int index) {
  const CoverImage& base = coverBase(index);
  int x = 0;
  int y = 0;
  const auto put = [&](const uint8_t byte) {
    for (int bit = 0; bit < 8; bit++) {
      if (!(byte & (0x80 >> bit))) renderer.drawPixel(x + bit, y, true);
    }
    x += 8;
    if (x >= kW) {
      x = 0;
      y++;
    }
  };
  size_t i = 0;
  while (i < base.size && y < kH) {
    const int8_t n = static_cast<int8_t>(base.data[i++]);
    if (n >= 0) {  // n+1 literal bytes
      for (int k = 0; k <= n && i < base.size; k++) put(base.data[i++]);
    } else if (n != -128 && i < base.size) {  // 1-n copies of the next byte
      const uint8_t b = base.data[i++];
      for (int k = 0; k < 1 - n; k++) put(b);
    }
  }
}

// Saves the screen memory (drawn in portrait, 480x800) as a 1-bit PNG. The image data is
// stored uncompressed: 48 KB, written in one go, no compressor needed on the reader.
inline bool savePng(const GfxRenderer& renderer, const std::string& path) {
  const uint8_t* fb = renderer.getFrameBuffer();
  const int panelW = renderer.getDisplayWidth();
  const int panelH = renderer.getDisplayHeight();
  const int panelRowBytes = renderer.getDisplayWidthBytes();
  if (!fb || panelW != kH || panelH != kW) return false;  // only the 800x480 panel of the X3/X4/X4 Pro

  const std::string tmp = path + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("TODO", tmp, file)) return false;
  PngWriter png{file};
  static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  png.raw(signature, 8);

  const uint8_t ihdr[13] = {0, 0, kW >> 8, kW & 0xFF, 0, 0, kH >> 8, kH & 0xFF, 1, 0, 0, 0, 0};  // 1-bit gray
  png.chunkStart(13, "IHDR");
  png.chunkData(ihdr, 13);
  png.chunkEnd();

  constexpr uint32_t dataLen = static_cast<uint32_t>(kH) * (kRowBytes + 1);  // filter byte + row
  static_assert(dataLen <= 0xFFFF, "one stored deflate block");
  png.chunkStart(2 + 5 + dataLen + 4, "IDAT");
  const uint8_t zhead[7] = {0x78, 0x01, 0x01, static_cast<uint8_t>(dataLen & 0xFF), static_cast<uint8_t>(dataLen >> 8),
                            static_cast<uint8_t>(~dataLen & 0xFF), static_cast<uint8_t>((~dataLen >> 8) & 0xFF)};
  png.chunkData(zhead, 7);
  uint8_t row[kRowBytes + 1];
  for (int y = 0; y < kH; y++) {
    row[0] = 0;  // no filter
    for (int b = 0; b < kRowBytes; b++) {
      uint8_t out = 0;
      for (int bit = 0; bit < 8; bit++) {
        // Portrait: logical (x, y) sits at panel (y, panelH - 1 - x); a set bit is white.
        const int x = b * 8 + bit;
        const int phyX = y;
        const int phyY = panelH - 1 - x;
        if (fb[phyY * panelRowBytes + phyX / 8] & (0x80 >> (phyX % 8))) out |= 0x80 >> bit;
      }
      row[b + 1] = out;
    }
    png.adler(row, sizeof(row));
    png.chunkData(row, sizeof(row));
  }
  const uint8_t adler[4] = {static_cast<uint8_t>(png.adlerB >> 8), static_cast<uint8_t>(png.adlerB),
                            static_cast<uint8_t>(png.adlerA >> 8), static_cast<uint8_t>(png.adlerA)};
  png.chunkData(adler, 4);
  png.chunkEnd();
  png.chunkStart(0, "IEND");
  png.chunkEnd();
  file.flush();
  file.close();
  if (!png.ok || !Storage.replaceFile(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

}  // namespace covermaker
}  // namespace todolist
