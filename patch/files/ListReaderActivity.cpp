#include "ListReaderActivity.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <builtinFonts/patrickhand_14_regular.h>
#include <builtinFonts/patrickhand_20_regular.h>
#include <builtinFonts/patrickhand_26_regular.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "ListStore.h"
#include "ReaderUtils.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kMenuHoldMs = 700;  // X3/X4: Confirm held this long = show/hide done

// Handwriting (Patrick Hand, SIL Open Font License), built into the firmware for the lists only.
constexpr int kListFontId = 0x4C495354;   // "LIST"
constexpr int kTitleFontId = 0x4C495448;  // "LITH"
constexpr int kSmallFontId = 0x4C495346;  // "LISF"
EpdFont listFont(&patrickhand_20_regular);
EpdFont titleFont(&patrickhand_26_regular);
EpdFont smallFont(&patrickhand_14_regular);

void registerFonts(GfxRenderer& renderer) {
  static bool done = false;
  if (done) return;
  renderer.insertFont(kListFontId, EpdFontFamily(20, &listFont));
  renderer.insertFont(kTitleFontId, EpdFontFamily(26, &titleFont));
  renderer.insertFont(kSmallFontId, EpdFontFamily(14, &smallFont));
  done = true;
}
}  // namespace

bool ListReaderActivity::loadBook() {
  registerFonts(renderer);
  listName = todolist::nameFromPath(bookPath);
  if (!todolist::loadList(bookPath, items)) return false;
  hideDone = todolist::isDoneHidden(listName);
  todolist::ensureCover();
  newSheet();
  return true;
}

void ListReaderActivity::newSheet() { paperSeed = esp_random() | 1u; }

bool ListReaderActivity::pageTurn(const bool isForward) {
  RenderLock lock(*this);
  const int last = static_cast<int>(pages.size()) - 1;
  if (isForward ? currentPage >= last : currentPage <= 0) return false;
  currentPage += isForward ? 1 : -1;
  selected = 0;
  newSheet();
  return true;
}

void ListReaderActivity::toggleEntry(const int pageEntry) {
  if (currentPage < 0 || currentPage >= static_cast<int>(pages.size())) return;
  const auto& entries = pages[currentPage].entries;
  if (pageEntry < 0 || pageEntry >= static_cast<int>(entries.size())) return;
  auto& item = items[entries[pageEntry].item];
  item.done = !item.done;
  saveFailed = !todolist::saveList(bookPath, items);
  if (saveFailed) item.done = !item.done;  // the screen must always show what is on the card
}

void ListReaderActivity::toggleHideDone() {
  hideDone = !hideDone;
  todolist::setDoneHidden(listName, hideDone);
  currentPage = 0;
  selected = 0;
  newSheet();
}

void ListReaderActivity::moveSelection(const int delta) {
  if (pages.empty()) return;
  const int count = static_cast<int>(pages[currentPage].entries.size());
  int sel = selected < 0 ? count - 1 : std::min(selected, count - 1);
  sel += delta;
  if (sel >= count) {
    if (currentPage + 1 >= static_cast<int>(pages.size())) return;  // already on the last entry
    currentPage++;
    sel = 0;
    newSheet();
  } else if (sel < 0) {
    if (currentPage == 0) return;
    currentPage--;
    sel = -1;  // last entry of the previous page (its size is known after the next layout)
    newSheet();
  }
  selected = sel;
}

void ListReaderActivity::loop() {
  rememberBookOnceRendered();
  if (handleBackNavigation()) return;

  const bool touch = mappedInput.hasTouch();

  if (touch) {
    // A tap in the margin ticks. Checked first, so the same tap cannot also turn the page.
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTapped(x, y) && x < paper.marginX) {
      bool changed = false;
      {
        RenderLock lock(*this);
        if (currentPage < static_cast<int>(pages.size())) {
          const int hit = todolist::entryAt(pages[currentPage], y - paper.listTop, 0);
          if (hit >= 0) {
            toggleEntry(hit);
            changed = true;
          }
        }
      }
      if (changed) {
        haptic_feedback::touchAction();
        requestUpdate();
      }
      return;
    }
    if (ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
      {
        RenderLock lock(*this);
        toggleHideDone();
      }
      haptic_feedback::touchAction();
      requestUpdate();
      return;
    }
  } else {
    // X3/X4: Confirm held = show/hide done, Confirm short = tick, Left/Right = select.
    if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kMenuHoldMs)) {
      {
        RenderLock lock(*this);
        toggleHideDone();
      }
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      {
        RenderLock lock(*this);
        if (currentPage < static_cast<int>(pages.size())) {
          const int count = static_cast<int>(pages[currentPage].entries.size());
          toggleEntry(selected < 0 ? count - 1 : std::min(selected, count - 1));
        }
      }
      requestUpdate();
      return;
    }
    const bool swap = mappedInput.isNavDirectionSwapped();
    const bool nextKey =
        mappedInput.wasPressed(swap ? MappedInputManager::Button::Left : MappedInputManager::Button::Right);
    const bool prevKey =
        mappedInput.wasPressed(swap ? MappedInputManager::Button::Right : MappedInputManager::Button::Left);
    if (nextKey || prevKey) {
      {
        RenderLock lock(*this);
        moveSelection(nextKey ? 1 : -1);
      }
      requestUpdate();
      return;
    }
  }

  // Page turns: swipe/tap as set in Settings, side keys, Power (if set), Home key, tilt.
  const auto touchTurn = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  bool prev = touchTurn.prev;
  bool next = touchTurn.next;
  if (!touch) {
    // On X3/X4 the front Left/Right keys select entries, so only the side keys turn pages.
    prev = prev || mappedInput.wasPressed(MappedInputManager::Button::PageBack);
    next = next || mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
           (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
            mappedInput.wasReleased(MappedInputManager::Button::Power));
  } else {
    const auto keys = ReaderUtils::detectPageTurn(mappedInput);
    prev = prev || keys.prev;
    next = next || keys.next;
  }
  if (!prev && !next) return;
  const bool changed = pageTurn(!prev);
  notePageTurn(!prev, changed);
  if (changed && (touchTurn.prev || touchTurn.next)) haptic_feedback::touchAction();
  if (changed) requestUpdate();
}

void ListReaderActivity::renderBook() {
  const bool touch = mappedInput.hasTouch();
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  const int lineHeight = std::max(1, renderer.getLineHeight(kListFontId));
  paper = listpaper::geometry(width, height, lineHeight, top, right, bottom, left);
  const int textWidth = std::max(40, paper.right - paper.textX);

  // Entries sit on the ruled lines: one text line = one ruled line, no extra gaps.
  const todolist::WrapFn wrap = [&](const std::string& text, const int maxLines) {
    return renderer.wrappedText(kListFontId, text.c_str(), textWidth, maxLines);
  };
  pages = todolist::layout(items, hideDone, wrap, paper.lineCount * paper.pitch, paper.pitch, 0);
  if (currentPage >= static_cast<int>(pages.size())) currentPage = std::max(0, static_cast<int>(pages.size()) - 1);
  if (!pages.empty()) {
    const int count = static_cast<int>(pages[currentPage].entries.size());
    if (selected < 0 || selected >= count) selected = count - 1;
  }

  renderer.clearScreen();
  const auto px = [&](const int x, const int y, const bool black) {
    if (x >= 0 && y >= 0 && x < width && y < height) renderer.drawPixel(x, y, black);
  };
  listpaper::paintPaper(paper, paperSeed, px);

  // Header: the list name as a headline (upper case, felt-pen strokes). A long name is written
  // one size smaller, and only cut with "..." if even that does not fit.
  const int titleBaseline = paper.firstRuleY - 6;
  const int titleWidth = std::max(40, paper.right - paper.textX - 4);
  std::string headline = todolist::toHeadline(listName);
  int headFont = kTitleFontId;
  if (renderer.getTextWidth(headFont, headline.c_str()) > titleWidth) {
    headFont = kListFontId;
    headline = renderer.truncatedText(headFont, headline.c_str(), titleWidth);
  }
  const int headAscender = renderer.getFontAscenderSize(headFont);
  for (int ox = 0; ox < 3; ox++) {  // three strokes side by side read like a felt pen
    renderer.drawText(headFont, paper.textX + ox, titleBaseline - headAscender, headline.c_str(), true);
  }

  // Footer, under a pen line: eye + "hide"/"show" (what the reader-menu gesture does next),
  // "3/9 done", and the page. Only an error message replaces it.
  listpaper::paintFooterRule(paper, px);
  const int smallAscender = renderer.getFontAscenderSize(kSmallFontId);
  const int footY = paper.footerBaseline - smallAscender;
  if (saveFailed) {
    renderer.drawText(kSmallFontId, paper.textX, footY, tr(STR_TODO_SAVE_FAILED), true);
  } else {
    const int doneCount = static_cast<int>(items.size()) - todolist::countOpen(items);
    if (doneCount > 0) {
      // the eye sits on the middle of the lower-case letters
      const int xHeight = std::max(6, smallAscender * 7 / 16);
      listpaper::paintEye(paper.textX + 16, paper.footerBaseline - xHeight / 2, hideDone, px);
      renderer.drawText(kSmallFontId, paper.textX + 40, footY, hideDone ? tr(STR_TODO_SHOW) : tr(STR_TODO_HIDE), true);
    }
    char count[32];
    snprintf(count, sizeof(count), tr(STR_TODO_DONE_COUNT), doneCount, static_cast<int>(items.size()));
    char page[16] = "";
    if (pages.size() > 1) snprintf(page, sizeof(page), "%d/%d", currentPage + 1, static_cast<int>(pages.size()));
    const int pageW = page[0] ? renderer.getTextWidth(kSmallFontId, page) : 0;
    if (page[0]) renderer.drawText(kSmallFontId, paper.right - pageW, footY, page, true);
    // "3/9 done" in a fixed column after "hide"/"show"; moved left only if a wide page number needs it
    const int countW = renderer.getTextWidth(kSmallFontId, count);
    const int countX = std::min(paper.textX + 140, paper.right - pageW - (page[0] ? 24 : 0) - countW);
    renderer.drawText(kSmallFontId, countX, footY, count, true);
  }

  // Entries: box in the margin on the entry's first line, text on the ruled lines.
  const int ascender = renderer.getFontAscenderSize(kListFontId);
  if (pages.empty()) {
    const char* msg = items.empty() ? tr(STR_TODO_EMPTY) : tr(STR_TODO_ALL_DONE);
    renderer.drawText(kListFontId, paper.textX, listpaper::ruleY(paper, 0) - 4 - ascender, msg, true);
  } else {
    const auto& page = pages[currentPage];
    for (int i = 0; i < static_cast<int>(page.entries.size()); i++) {
      const auto& entry = page.entries[i];
      const int firstLine = entry.top / paper.pitch;
      listpaper::paintBox(paper, firstLine, items[entry.item].done, entry.item + 1, px);
      for (int l = 0; l < static_cast<int>(entry.lines.size()); l++) {
        renderer.drawText(kListFontId, paper.textX, listpaper::ruleY(paper, firstLine + l) - 4 - ascender,
                          entry.lines[l].c_str(), true);
      }
      if (!touch && i == selected) {
        listpaper::paintSelection(paper, firstLine, static_cast<int>(entry.lines.size()), px);
      }
    }
  }

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  markPageRendered();
}

ScreenshotInfo ListReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  snprintf(info.title, sizeof(info.title), "%s", listName.c_str());
  info.totalPages = static_cast<int>(pages.size());
  info.currentPage = currentPage + 1;
  const int open = todolist::countOpen(items);
  const int total = static_cast<int>(items.size());
  info.progressPercent = total > 0 ? ((total - open) * 100) / total : 0;
  info.endProgressPercent = info.progressPercent;
  return info;
}
