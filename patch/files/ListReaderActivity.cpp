#include "ListReaderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "ListStore.h"
#include "ReaderUtils.h"
#include "fontIds.h"

namespace {
// Tick strip on touch screens: about 1.5 cm from the left screen edge (X4 Pro: ~220 dpi).
constexpr int kStripPx = 130;
constexpr unsigned long kMenuHoldMs = 700;  // X3/X4: Confirm held this long = show/hide done
}  // namespace

bool ListReaderActivity::loadBook() {
  listName = todolist::nameFromPath(bookPath);
  if (!todolist::loadList(bookPath, items)) return false;
  hideDone = todolist::isDoneHidden(listName);
  return true;
}

bool ListReaderActivity::pageTurn(const bool isForward) {
  RenderLock lock(*this);
  const int last = static_cast<int>(pages.size()) - 1;
  if (isForward ? currentPage >= last : currentPage <= 0) return false;
  currentPage += isForward ? 1 : -1;
  selected = 0;
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
  } else if (sel < 0) {
    if (currentPage == 0) return;
    currentPage--;
    sel = -1;  // last entry of the previous page (its size is known after the next layout)
  }
  selected = sel;
}

void ListReaderActivity::loop() {
  rememberBookOnceRendered();
  if (handleBackNavigation()) return;

  const bool touch = mappedInput.hasTouch();

  if (touch) {
    // A tap in the strip ticks. Checked first, so the same tap cannot also turn the page.
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTapped(x, y) && x < stripRight) {
      bool changed = false;
      {
        RenderLock lock(*this);
        if (currentPage < static_cast<int>(pages.size())) {
          const int hit = todolist::entryAt(pages[currentPage], y - areaTop, entryGap);
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
    const bool nextKey = mappedInput.wasPressed(swap ? MappedInputManager::Button::Left : MappedInputManager::Button::Right);
    const bool prevKey = mappedInput.wasPressed(swap ? MappedInputManager::Button::Right : MappedInputManager::Button::Left);
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
    const auto pressed = [&](const MappedInputManager::Button b) { return mappedInput.wasPressed(b); };
    prev = prev || pressed(MappedInputManager::Button::PageBack);
    next = next || pressed(MappedInputManager::Button::PageForward) ||
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
  top += SETTINGS.screenMargin;
  right += SETTINGS.screenMargin;
  bottom += SETTINGS.screenMargin;
  left += SETTINGS.screenMargin;
  const int width = renderer.getScreenWidth() - left - right;
  const int screenHeight = renderer.getScreenHeight();

  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = std::max(1, renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression()));
  const int box = std::clamp(renderer.getFontAscenderSize(fontId), 16, 40);
  const int boxGap = std::max(10, box / 2);
  const int textX = left + box + boxGap;
  const int textWidth = std::max(40, left + width - textX);
  entryGap = std::max(8, lineHeight / 2);
  stripRight = std::max(textX, std::min(kStripPx, renderer.getScreenWidth() * 3 / 10));

  // Header: list name, then "3 of 5 open" (and the hidden note), then a rule.
  const int titleFont = UI_12_FONT_ID;
  const int infoFont = UI_10_FONT_ID;
  const int titleH = renderer.getLineHeight(titleFont);
  const int infoH = renderer.getLineHeight(infoFont);

  // Layout first, so the header can show the page count.
  areaTop = top + titleH + infoH + 14;
  const int areaHeight = screenHeight - bottom - areaTop;
  const todolist::WrapFn wrap = [&](const std::string& text, const int maxLines) {
    return renderer.wrappedText(fontId, text.c_str(), textWidth, maxLines);
  };
  pages = todolist::layout(items, hideDone, wrap, areaHeight, lineHeight, entryGap);
  if (currentPage >= static_cast<int>(pages.size())) currentPage = std::max(0, static_cast<int>(pages.size()) - 1);
  if (!pages.empty()) {
    const int count = static_cast<int>(pages[currentPage].entries.size());
    if (selected < 0 || selected >= count) selected = count - 1;
  }

  renderer.clearScreen();

  char pageText[16] = "";
  if (pages.size() > 1) snprintf(pageText, sizeof(pageText), "%d/%d", currentPage + 1, static_cast<int>(pages.size()));
  const int pageW = pageText[0] ? renderer.getTextWidth(infoFont, pageText) + 12 : 0;
  const std::string title = renderer.truncatedText(titleFont, listName.c_str(), width, EpdFontFamily::BOLD);
  renderer.drawText(titleFont, left, top, title.c_str(), true, EpdFontFamily::BOLD);

  char info[96];
  if (saveFailed) {
    snprintf(info, sizeof(info), "%s", tr(STR_TODO_SAVE_FAILED));
  } else {
    snprintf(info, sizeof(info), tr(STR_TODO_OPEN_COUNT), todolist::countOpen(items), static_cast<int>(items.size()));
    if (hideDone) {
      const size_t used = strlen(info);
      snprintf(info + used, sizeof(info) - used, " \xC2\xB7 %s", tr(STR_TODO_DONE_HIDDEN));
    }
  }
  const std::string infoLine = renderer.truncatedText(infoFont, info, width - pageW);
  renderer.drawText(infoFont, left, top + titleH, infoLine.c_str());
  if (pageText[0]) {
    renderer.drawText(infoFont, left + width - renderer.getTextWidth(infoFont, pageText), top + titleH, pageText);
  }
  renderer.drawLine(left, areaTop - 7, left + width, areaTop - 7, 2, true);

  if (pages.empty()) {
    const char* msg = items.empty() ? tr(STR_TODO_EMPTY) : tr(STR_TODO_ALL_DONE);
    renderer.drawCenteredText(UI_12_FONT_ID, areaTop + areaHeight / 3, msg);
  } else {
    const auto& page = pages[currentPage];
    for (int i = 0; i < static_cast<int>(page.entries.size()); i++) {
      const auto& entry = page.entries[i];
      const int y = areaTop + entry.top;
      const bool done = items[entry.item].done;

      // Box, and for done entries a bold X in it.
      const int bx = left;
      const int by = y + std::max(0, (lineHeight - box) / 2);
      renderer.drawRect(bx, by, box, box, 3, true);
      if (done) {
        const int p = std::max(4, box / 5);
        renderer.drawLine(bx + p, by + p, bx + box - 1 - p, by + box - 1 - p, 3, true);
        renderer.drawLine(bx + box - 1 - p, by + p, bx + p, by + box - 1 - p, 3, true);
      }

      for (int l = 0; l < static_cast<int>(entry.lines.size()); l++) {
        renderer.drawText(fontId, textX, y + l * lineHeight, entry.lines[l].c_str(), true);
      }

      // X3/X4: frame around the selected entry.
      if (!touch && i == selected) {
        renderer.drawRoundedRect(left - 6, y - 5, width + 12, entry.height + 10, 2, 8, true);
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
