#!/usr/bin/env python3
"""Adds to-do lists to a CrossPoint Reader source tree.

Usage: python3 apply_todolist.py <path-to-crosspoint-reader> [owner/repo]

With owner/repo, the on-device "Check for updates" asks that GitHub repository's latest
release instead of the official one, so updates keep the lists.

Every change is anchored to a piece of the original code. If CrossPoint changes one of
those places, the script stops with a clear message instead of building something broken.
"""

import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

READER_FILES = ("ListPath.h", "ListCore.h", "ListStore.h", "ListPaper.h", "ListCover.h", "ListCoverImages.h",
                "ListReaderActivity.h", "ListReaderActivity.cpp")
TXT_FILES = ("ListPath.h",)
FONT_FILES = ("patrickhand_11_regular.h", "patrickhand_20_regular.h", "patrickhand_26_regular.h")
NETWORK_FILES = ("ListsWeb.h",)

# All list texts are English (like the rest of the reader's add-on texts).
STRINGS = {
    "english.yaml": {
        "STR_TODO_HINT_SWIPE": "swipe up",
        "STR_TODO_HINT_TAP": "tap",
        "STR_TODO_HINT_HERE": "tap here",
        "STR_TODO_HINT_HOLD": "hold OK",
        "STR_TODO_HIDE_CHECKED": "to hide checked",
        "STR_TODO_SHOW_CHECKED": "to show checked",
        "STR_TODO_EMPTY_TOUCH": "tap a line to write",
        "STR_TODO_EMPTY_KEYS": "press OK to write",
        "STR_TODO_ALL_DONE": "All done",
        "STR_TODO_EDIT": "edit",
        "STR_TODO_DELETE": "delete",
        "STR_TODO_NEW_LIST": "new list",
        "STR_TODO_RENAME": "rename list",
        "STR_TODO_ADD_ITEMS": "add items",
        "STR_TODO_UNDO": "undo delete",
        "STR_TODO_TO_UNDO": "to undo",
        "STR_TODO_MENU_HIDE": "hide checked",
        "STR_TODO_MENU_SHOW": "show checked",
        "STR_TODO_KB_ADD": "New items (separate with , or ;)",
        "STR_TODO_KB_EDIT": "Edit item (empty = delete)",
        "STR_TODO_KB_NEW": "Name of the new list",
        "STR_TODO_KB_RENAME": "New name for this list",
        "STR_TODO_NAME_INVALID": "Not a usable list name",
        "STR_TODO_NAME_TAKEN": "A list with this name exists",
        "STR_TODO_SAVE_FAILED": "Could not save the list",
    },
}


def fail(msg: str) -> None:
    print(f"TODOLIST PATCH FAILED: {msg}", file=sys.stderr)
    sys.exit(1)


def replace_once(text: str, old: str, new: str, what: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"'{what}': expected the anchor exactly once, found it {count} times. "
             f"CrossPoint probably changed this spot; the patch needs an update.")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) not in (2, 3):
        fail("usage: apply_todolist.py <crosspoint-reader folder> [owner/repo]")
    root = Path(sys.argv[1])
    reader_dir = root / "src" / "activities" / "reader"
    network_dir = root / "src" / "network"
    reader = reader_dir / "ReaderActivity.cpp"
    web = network_dir / "CrossPointWebServer.cpp"
    txt_dir = root / "lib" / "Txt"
    txt = txt_dir / "Txt.cpp"
    for f in (reader, web, txt):
        if not f.is_file():
            fail(f"{f} not found")

    for name in READER_FILES:
        shutil.copy(HERE / "files" / name, reader_dir / name)
    for name in NETWORK_FILES:
        shutil.copy(HERE / "files" / name, network_dir / name)
    for name in TXT_FILES:
        shutil.copy(HERE / "files" / name, txt_dir / name)
    fonts_dir = root / "lib" / "EpdFont" / "builtinFonts"
    if not fonts_dir.is_dir():
        fail(f"{fonts_dir} not found")
    for name in FONT_FILES:
        shutil.copy(HERE / "files" / "fonts" / name, fonts_dir / name)

    # 1) Files in /Lists open as a list instead of the text reader.
    src = reader.read_text(encoding="utf-8")
    if '#include "ListReaderActivity.h"' in src:
        fail("patch is already applied to this tree")
    src = replace_once(src, '#include "EpubReaderActivity.h"\n',
                       '#include "EpubReaderActivity.h"\n#include "ListCore.h"\n#include "ListReaderActivity.h"\n',
                       "reader include")
    src = replace_once(src,
                       "  if (FsHelpers::hasXtcExtension(path)) {\n",
                       "  if (todolist::isListPath(path)) {  // to-do list (Crosspoint-ToDoList)\n"
                       "    activity = makeUniqueNoThrow<ListReaderActivity>(renderer, mappedInput, std::move(path),\n"
                       "                                                     allowFastInitialRefresh);\n"
                       "  } else if (FsHelpers::hasXtcExtension(path)) {\n",
                       "open lists")
    reader.write_text(src, encoding="utf-8")

    # 2) Web addresses for adding entries from a phone or computer.
    src = web.read_text(encoding="utf-8")
    src = replace_once(src, '#include "CrossPointWebServer.h"\n',
                       '#include "CrossPointWebServer.h"\n#include "ListsWeb.h"\n', "web include")
    src = replace_once(src,
                       '  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });\n',
                       '  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });\n'
                       '  // to-do lists (Crosspoint-ToDoList)\n'
                       '  server->on("/api/lists", HTTP_GET, [this] { todolist::handleListIndex(*server); });\n'
                       '  server->on("/api/lists/add", HTTP_POST, [this] { todolist::handleListAdd(*server); });\n',
                       "web routes")
    web.write_text(src, encoding="utf-8")

    # 3) First start: an empty "To do" list in /Lists, so there is always one to open.
    main = root / "src" / "main.cpp"
    src = main.read_text(encoding="utf-8")
    src = replace_once(src, '#include "SdCardFontSystem.h"\n',
                       '#include "SdCardFontSystem.h"\n#include "activities/reader/ListStore.h"\n', "main include")
    src = replace_once(src, "  sdFontSystem.begin(renderer);\n",
                       "  sdFontSystem.begin(renderer);\n"
                       "  todolist::ensureStarterList();  // Crosspoint-ToDoList\n"
                       "  todolist::ensureCover();\n",
                       "starter list")
    main.write_text(src, encoding="utf-8")

    # 3b) On the home screen every list shows one of the five notepad covers.
    src = txt.read_text(encoding="utf-8")
    src = replace_once(src, '#include "TxtToHtml.h"\n',
                       '#include "ListPath.h"\n#include "TxtToHtml.h"\n', "txt include")
    src = replace_once(src,
                       "std::string Txt::findCompanionCoverImage(const std::string& filepath) {\n",
                       "std::string Txt::findCompanionCoverImage(const std::string& filepath) {\n"
                       "  if (todolist::isListPath(filepath)) {  // to-do list notepad cover (Crosspoint-ToDoList)\n"
                       "    const std::string listCover = todolist::coverPath(todolist::coverIndexFor(filepath));\n"
                       "    if (Storage.exists(listCover.c_str())) return listCover;\n"
                       "  }\n",
                       "list covers")
    txt.write_text(src, encoding="utf-8")

    # 4) Texts. Languages without their own entry fall back to English.
    translations = root / "lib" / "I18n" / "translations"
    for file_name, entries in STRINGS.items():
        yaml_file = translations / file_name
        if not yaml_file.is_file():
            fail(f"'translations': {file_name} not found")
        text = yaml_file.read_text(encoding="utf-8")
        for key, value in entries.items():
            if f"\n{key}:" in text:
                fail(f"'translations': {key} already exists in {file_name}")
            text = text.rstrip("\n") + f'\n{key}: "{value}"\n'
        yaml_file.write_text(text, encoding="utf-8")

    # 5) On-device updates come from this repository's releases (they include the lists).
    if len(sys.argv) == 3:
        repo = sys.argv[2].strip()
        if repo.count("/") != 1 or not all(part for part in repo.split("/")):
            fail(f"'{repo}' is not in owner/repo form")
        ota = network_dir / "OtaUpdater.cpp"
        ota_src = replace_once(ota.read_text(encoding="utf-8"),
                               '"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest"',
                               f'"https://api.github.com/repos/{repo}/releases/latest"', "update source")
        ota.write_text(ota_src, encoding="utf-8")
    print("To-do list patch applied.")


if __name__ == "__main__":
    main()
