#pragma once
// To-do list screen (added by the Crosspoint-ToDoList patch).
//
// Opens every "/Lists/<name>.txt" instead of the normal text reader and shows it as a sheet of
// a spiral notepad in handwriting. One line of the file is one item; every change is written
// back to the file at once.
//
//  X4 Pro (touch):
//    tap a box (left margin)        tick / untick
//    tap an empty line              keyboard: add items (commas separate them)
//    long press an item             small buttons: edit / delete (delete has an undo)
//    tap or hold the headline       small buttons: new list / rename / delete list (asks first)
//    reader-menu gesture            hide / show checked items (remembered per list)
//    swipe or tap elsewhere         turn pages, as set in Settings
//  X3 / X4 (keys):
//    Left/Right select, Confirm ticks, side keys turn pages,
//    Confirm held opens a small menu with all of the above.

#include <cstdint>
#include <string>
#include <vector>

#include "ListCore.h"
#include "ListPaper.h"
#include "ReaderActivity.h"

class ListReaderActivity final : public ReaderActivity {
  enum class Action : uint8_t { None, Edit, Delete, Add, NewList, Rename, DeleteList, ToggleHide, Undo };
  enum class Overlay : uint8_t { None, Item, Title, Menu };

  struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    Action action = Action::None;
  };

  std::string listName;
  std::vector<todolist::Item> items;
  bool hideDone = false;
  const char* message = nullptr;  // one-off note in the footer (error, name taken ...)

  // Undo for the last delete, until the page changes or something else is edited.
  bool undoPending = false;
  int undoIndex = 0;
  todolist::Item undoItem;

  // Overlay buttons (touch) or menu (keys); drawn by renderBook(), read by loop().
  Overlay overlay = Overlay::None;
  int overlayEntry = -1;  // entry on the current page the item buttons belong to
  int menuSelected = 0;
  std::vector<Button> buttons;

  // Filled by renderBook() and used by loop() for taps and key selection (under RenderLock).
  std::vector<todolist::Page> pages;
  listpaper::Geometry paper;
  int currentPage = 0;
  int selected = 0;        // entry on the current page (X3/X4 only); -1 = last entry of the page
  uint32_t paperSeed = 1;  // which spiral holes are torn: fixed per list and page

  bool loadBook() override;
  std::string getBookTitle() const override { return listName; }
  void renderBook() override;
  bool isAtEndOfBook() const override { return false; }

  void newSheet();
  bool save();
  void toggleEntry(int pageEntry);
  void toggleHideDone();
  void moveSelection(int delta);
  int itemOfEntry(int pageEntry) const;
  int selectedEntry() const;
  void runAction(Action action, int pageEntry);
  void openKeyboard(Action action, int item);
  void confirmDeleteList();
  void drawOverlay(bool touch);
  bool handleOverlayInput(bool touch);

 public:
  explicit ListReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("ListReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~ListReaderActivity() override = default;

  void loop() override;
  bool pageTurn(bool isForward) override;
  ScreenshotInfo getScreenshotInfo() const override;
};
