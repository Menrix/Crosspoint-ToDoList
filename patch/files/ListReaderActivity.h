#pragma once
// To-do list screen (added by the Crosspoint-ToDoList patch).
//
// Opens every "/Lists/<name>.txt" instead of the normal text reader and shows it as a sheet of
// a spiral notepad in handwriting. One line of the file is one entry; ticking writes "[x]" back
// into the file at once.
//
//  X4 Pro (touch): tap the margin on the left (where the boxes are) to tick or untick an entry.
//                  Everywhere else the normal reader controls apply (swipe or tap to turn pages).
//  X3 / X4 (keys): front Left/Right move the selection, Confirm ticks, the side keys turn pages.
//  All devices:    the reader-menu gesture (X4 Pro) or Confirm long press (X3/X4) switches
//                  between showing and hiding done entries, remembered per list.

#include <cstdint>
#include <string>
#include <vector>

#include "ListCore.h"
#include "ListPaper.h"
#include "ReaderActivity.h"

class ListReaderActivity final : public ReaderActivity {
  std::string listName;
  std::vector<todolist::Item> items;
  bool hideDone = false;
  bool saveFailed = false;

  // Filled by renderBook() and used by loop() for taps and key selection (under RenderLock).
  std::vector<todolist::Page> pages;
  listpaper::Geometry paper;
  int currentPage = 0;
  int selected = 0;        // entry on the current page (X3/X4 only); -1 = last entry of the page
  uint32_t paperSeed = 1;  // which spiral holes are torn: new for every page shown

  bool loadBook() override;
  std::string getBookTitle() const override { return listName; }
  void renderBook() override;
  bool isAtEndOfBook() const override { return false; }

  void newSheet();
  void toggleEntry(int pageEntry);
  void toggleHideDone();
  void moveSelection(int delta);

 public:
  explicit ListReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("ListReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~ListReaderActivity() override = default;

  void loop() override;
  bool pageTurn(bool isForward) override;
  ScreenshotInfo getScreenshotInfo() const override;
};
