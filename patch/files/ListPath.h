#pragma once
// Which files are lists, and which notepad cover each list shows (Crosspoint-ToDoList patch).
//
// Pure C++17 with no other includes from the patch, because the text-book code in lib/Txt
// needs it too: the patch copies this same file to src/activities/reader and to lib/Txt.

#include <cstdint>
#include <cstdio>
#include <string>

namespace todolist {

// Folder that holds the lists.
constexpr const char* kListsDir = "/Lists";

// The five notepad covers live here; the reader writes them (see ListStore.h).
constexpr const char* kCoverDir = "/.crosspoint/todolist";
constexpr int kCoverCount = 5;

// True for "/Lists/<name>.txt" (folder and extension in any letter case), one level deep.
inline bool isListPath(const std::string& path) {
  const std::string dir = std::string(kListsDir) + "/";
  if (path.size() <= dir.size() + 4) return false;
  for (size_t i = 0; i < dir.size(); i++) {
    char a = path[i];
    char b = dir[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
    if (a != b) return false;
  }
  const std::string rest = path.substr(dir.size());
  if (rest.find('/') != std::string::npos) return false;
  // Hidden files are never lists, e.g. the "._Name.txt" files macOS leaves on SD cards.
  if (rest[0] == '.') return false;
  std::string ext = rest.substr(rest.size() - 4);
  for (char& c : ext) c = static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
  return ext == ".txt";
}

inline std::string coverPath(const int index) {
  return std::string(kCoverDir) + "/cover_" + std::to_string(index + 1) + ".png";
}

// Each list keeps one of the five covers, picked from its file name (letter case ignored),
// so a list always looks the same on the home screen and different lists look different.
inline int coverIndexFor(const std::string& listPath) {
  const size_t slash = listPath.find_last_of('/');
  uint32_t h = 2166136261u;
  for (size_t i = slash == std::string::npos ? 0 : slash + 1; i < listPath.size(); i++) {
    char c = listPath[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  }
  return static_cast<int>(h % kCoverCount);
}

// The cover with the list's own name on it, written by the reader (ListCoverMaker.h) the first
// time the list is opened. Until then the list shows its plain notepad cover.
inline std::string namedCoverPath(const std::string& listPath) {
  const size_t slash = listPath.find_last_of('/');
  uint32_t h = 2166136261u;
  for (size_t i = slash == std::string::npos ? 0 : slash + 1; i < listPath.size(); i++) {
    char c = listPath[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  }
  char name[24];
  snprintf(name, sizeof(name), "/list_%08x.png", static_cast<unsigned>(h));
  return std::string(kCoverDir) + name;
}

}  // namespace todolist
