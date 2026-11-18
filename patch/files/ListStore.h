#pragma once
// Reading and writing list files on the SD card (added by the Crosspoint-ToDoList patch).
// The file rules themselves live in ListCore.h; this is only the SD card part.

#include <HalStorage.h>
#include <Logging.h>
#include <WString.h>

#include <string>
#include <vector>

#include "ListCore.h"
#include "ListCover.h"

namespace todolist {

// Per-list "hide done" choice. One file for all lists; names of lists that no longer exist
// are dropped whenever it is saved, so deleting a list leaves nothing behind for long.
constexpr const char* kHiddenFile = "/.crosspoint/todolist-hidden.txt";

// Loads a list. A missing file is an empty list. If a save was cut off between removing the
// old file and renaming the new one, the complete ".tmp" copy is taken back first.
inline bool loadList(const std::string& path, std::vector<Item>& items) {
  items.clear();
  const std::string tmp = path + ".tmp";
  if (!Storage.exists(path.c_str()) && Storage.exists(tmp.c_str())) {
    Storage.rename(tmp.c_str(), path.c_str());
  }
  if (!Storage.exists(path.c_str())) return true;
  std::string text;
  if (!Storage.readFileToString("TODO", path, kMaxListBytes, text)) {
    // readFileToString also refuses an empty file; that is simply an empty list.
    HalFile f;
    if (Storage.openFileForRead("TODO", path, f) && f.fileSize() == 0) return true;
    LOG_ERR("TODO", "Cannot read list %s", path.c_str());
    return false;
  }
  items = parse(text);
  return true;
}

// Saves a list: written beside the old file and swapped in only when complete.
inline bool saveList(const std::string& path, const std::vector<Item>& items) {
  Storage.ensureDirectoryExists(kListsDir);
  const std::string text = serialize(items);
  if (!Storage.writeFile(path.c_str(), String(text.c_str()))) {
    LOG_ERR("TODO", "Cannot save list %s", path.c_str());
    return false;
  }
  return true;
}

// Writes the post-it cover (/Lists/cover.bmp) once, if it is not there yet.
inline void ensureCover() {
  const std::string path = std::string(kListsDir) + "/cover.bmp";
  if (Storage.exists(path.c_str())) return;
  Storage.ensureDirectoryExists(kListsDir);
  const std::vector<uint8_t> bmp = makeCoverBmp();
  const std::string tmp = path + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("TODO", tmp, f)) return;
    const bool complete = f.write(bmp.data(), bmp.size()) == bmp.size();
    f.flush();
    if (!complete) {
      f.close();
      Storage.remove(tmp.c_str());
      return;
    }
  }
  if (!Storage.replaceFile(tmp.c_str(), path.c_str())) Storage.remove(tmp.c_str());
}

inline std::vector<std::string> loadHiddenNames() {
  std::vector<std::string> names;
  std::string text;
  if (!Storage.readFileToString("TODO", kHiddenFile, 16 * 1024, text)) return names;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) names.push_back(line);
    start = end + 1;
  }
  return names;
}

inline bool isDoneHidden(const std::string& name) {
  for (const auto& n : loadHiddenNames()) {
    if (n == name) return true;
  }
  return false;
}

inline void setDoneHidden(const std::string& name, const bool hidden) {
  std::string out;
  for (const auto& n : loadHiddenNames()) {
    if (n == name) continue;
    if (!Storage.exists(pathFor(n).c_str())) continue;  // list was deleted: forget it
    out += n;
    out += '\n';
  }
  if (hidden) {
    out += name;
    out += '\n';
  }
  Storage.ensureDirectoryExists("/.crosspoint");
  if (!Storage.writeFile(kHiddenFile, String(out.c_str()))) LOG_ERR("TODO", "Cannot save hidden-list setting");
}

}  // namespace todolist
