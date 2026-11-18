#include "ListReaderActivity.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <builtinFonts/patrickhand_11_regular.h>
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
EpdFont smallFont(&patrickhand_11_regular);

void registerFonts(GfxRenderer& renderer) {
  static bool done = false;
  if (done) return;
  renderer.insertFont(kListFontId, EpdFontFamily(20, &listFont));
  renderer.insertFont(kTitleFontId, EpdFontFamily(26, &titleFont));
  renderer.insertFont(kSmallFontId, EpdFontFamily(11, &smallFont));
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
    const bool footerTap = SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_OFF &&
                           mappedInput.wasScreenTapped(x, y) && y >= paper.footerRuleY - 10;
    if (footerTap || ReaderUtils::isTouchMenuGesture(renderer, mappedInput) ||
        mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu) {
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

  // Footer, under a pen line: an eye in the box column (open = checked lines shown, closed =
  // hidden), a hint that names the gesture set in Settings, and the page ("1/1" too).
  listpaper::paintFooterRule(paper, px);
  const int smallAscender = renderer.getFontAscenderSize(kSmallFontId);
  const int footY = paper.footerBaseline - smallAscender;
  char page[16];
  snprintf(page, sizeof(page), "%d/%d", currentPage + 1, std::max(1, static_cast<int>(pages.size())));
  const int pageW = renderer.getTextWidth(kSmallFontId, page);
  renderer.drawText(kSmallFontId, paper.right - pageW, footY, page, true);
  const int doneCount = static_cast<int>(items.size()) - todolist::countOpen(items);
  if (saveFailed) {
    renderer.drawText(kSmallFontId, paper.textX, footY, tr(STR_TODO_SAVE_FAILED), true);
  } else if (doneCount > 0) {
    const int xHeight = std::max(6, smallAscender * 7 / 16);  // eye on the middle of the lower-case letters
    listpaper::paintEye(paper.boxX + paper.boxSize / 2, paper.footerBaseline - xHeight / 2, hideDone, px);
    const char* gesture = tr(STR_TODO_HINT_HOLD);
    if (touch) {
      switch (SETTINGS.showReaderMenu) {
        case CrossPointSettings::READER_MENU_SWIPE_UP:
          gesture = tr(STR_TODO_HINT_SWIPE);
          break;
        case CrossPointSettings::READER_MENU_TAP:
          gesture = tr(STR_TODO_HINT_TAP);
          break;
        default:
          gesture = tr(STR_TODO_HINT_HERE);  // no menu gesture: a tap on this line switches
          break;
      }
    }
    char hint[96];
    snprintf(hint, sizeof(hint), "%s %s", gesture, hideDone ? tr(STR_TODO_SHOW_CHECKED) : tr(STR_TODO_HIDE_CHECKED));
    const std::string fitted = renderer.truncatedText(kSmallFontId, hint, std::max(40, paper.right - pageW - 16 - paper.textX));
    renderer.drawText(kSmallFontId, paper.textX, footY, fitted.c_str(), true);
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
