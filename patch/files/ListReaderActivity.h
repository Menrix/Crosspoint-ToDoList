#pragma once
// To-do list screen (added by the Crosspoint-ToDoList patch).
//
// Opens every "/Lists/<name>.txt" instead of the normal text reader. One line of the file is
// one entry; ticking writes "[x]" back into the file at once.
//
//  X4 Pro (touch): tap the strip on the left to tick or untick an entry. Everywhere else the
//                  normal reader controls apply (swipe or tap to turn pages, as set in Settings).
//  X3 / X4 (keys): front Left/Right move the selection, Confirm ticks, the side keys turn pages.
//  All devices:    the reader-menu gesture (X4 Pro) or Confirm long press (X3/X4) switches
//                  between showing and hiding done entries, remembered per list.

#include <string>
#include <vector>

#include "ListCore.h"
#include "ReaderActivity.h"

class ListReaderActivity final : public ReaderActivity {
  std::string listName;
  std::vector<todolist::Item> items;
  bool hideDone = false;
  bool saveFailed = false;

  // Filled by renderBook() and used by loop() for taps and key selection (under RenderLock).
  std::vector<todolist::Page> pages;
  int currentPage = 0;
  int selected = 0;  // entry on the current page (X3/X4 only); -1 = last entry of the page
  int areaTop = 0;
  int entryGap = 0;
  int stripRight = 0;

  bool loadBook() override;
  std::string getBookTitle() const override { return listName; }
  void renderBook() override;
  bool isAtEndOfBook() const override { return false; }

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
