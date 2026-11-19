#pragma once
// Reading and writing list files on the SD card (added by the Crosspoint-ToDoList patch).
// The file rules themselves live in ListCore.h; this is only the SD card part.

#include <HalStorage.h>
#include <Logging.h>
#include <WString.h>

#include <functional>
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

// True if /Lists holds at least one list.
inline bool hasAnyList() {
  if (!Storage.exists(kListsDir)) return false;
  for (const String& file : Storage.listFiles(kListsDir, 200)) {
    if (isListPath(std::string(kListsDir) + "/" + file.c_str())) return true;
  }
  return false;
}

// There is always at least one list, because new lists are made from inside a list (tap the
// headline). On first start, and whenever the last list was deleted (on the reader, in the file
// browser or on the computer), an empty "TO DO" list is made. Names are in capitals, like the headline.
inline void ensureStarterList() {
  if (hasAnyList()) return;
  Storage.ensureDirectoryExists(kListsDir);
  Storage.writeFile(pathFor("TO DO").c_str(), String(""));
}

// Creates an empty list. False if the name is unusable or the file cannot be written.
inline bool createList(const std::string& name) {
  if (name.empty()) return false;
  Storage.ensureDirectoryExists(kListsDir);
  const std::string path = pathFor(name);
  if (Storage.exists(path.c_str())) return true;
  return Storage.writeFile(path.c_str(), String(""));
}

// The home screen keeps a scaled copy of each cover. After new covers, drop those copies
// for the lists so the home screen makes fresh ones. (Text books keep their cache in
// "/.crosspoint/epub_<hash of the path>", like EPUBs.)
inline void clearListThumbs() {
  for (const String& file : Storage.listFiles(kListsDir, 200)) {
    const std::string path = std::string(kListsDir) + "/" + file.c_str();
    if (!isListPath(path)) continue;
    const std::string dir = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
    if (!Storage.exists(dir.c_str())) continue;
    for (const String& cached : Storage.listFiles(dir.c_str(), 50)) {
      if (cached.startsWith("thumb_")) Storage.remove((dir + "/" + cached.c_str()).c_str());
    }
  }
}

// Writes one picture file in full or not at all (beside the old one, swapped in when complete).
inline bool writeWholeFile(const std::string& path, const uint8_t* data, const size_t size) {
  const std::string tmp = path + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("TODO", tmp, f)) return false;
    const bool complete = f.write(data, size) == size;
    f.flush();
    if (!complete) {
      f.close();
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

// Puts the five notepad covers into /.crosspoint/todolist when one is missing or from an older
// version (recognised by its size). The home screen picks one per list (see ListPath.h).
// Earlier versions kept a single cover in /Lists/cover.bmp; that file is removed.
inline void ensureCover() {
  bool changed = false;
  const std::string oldCover = std::string(kListsDir) + "/cover.bmp";
  if (Storage.exists(oldCover.c_str())) {
    Storage.remove(oldCover.c_str());
    changed = true;
  }
  Storage.ensureDirectoryExists(kCoverDir);
  for (int i = 0; i < kCoverCount; i++) {
    const CoverImage& image = coverImage(i);
    const std::string path = coverPath(i);
    if (Storage.exists(path.c_str())) {
      HalFile existing;
      if (Storage.openFileForRead("TODO", path, existing) && existing.fileSize() == image.size) continue;
    }
    if (writeWholeFile(path, image.data, image.size)) changed = true;
  }
  if (changed) clearListThumbs();
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

inline void setDoneHidden(const std::string& name, bool hidden);

// Renames a list file and carries its "hide checked" choice over. False if the new name is
// unusable or already taken.
inline bool renameList(const std::string& oldName, const std::string& newName) {
  if (newName.empty() || newName == oldName) return false;
  const std::string from = pathFor(oldName);
  const std::string to = pathFor(newName);
  // The SD card ignores letter case, so "Einkauf" -> "EINKAUF" would look taken; go via a
  // temporary name for that case.
  if (detail::foldKey(oldName) == detail::foldKey(newName)) {
    const std::string tmp = from + ".rename";
    if (!Storage.rename(from.c_str(), tmp.c_str())) return false;
    if (!Storage.rename(tmp.c_str(), to.c_str())) {
      Storage.rename(tmp.c_str(), from.c_str());
      return false;
    }
  } else {
    if (Storage.exists(to.c_str())) return false;
    if (!Storage.rename(from.c_str(), to.c_str())) return false;
  }
  if (isDoneHidden(oldName)) setDoneHidden(newName, true);
  return true;
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
