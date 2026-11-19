#include "ListReaderActivity.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <builtinFonts/patrickhand_11_regular.h>
#include <builtinFonts/patrickhand_20_regular.h>
#include <builtinFonts/patrickhand_26_regular.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "ListCoverMaker.h"
#include "ListStore.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookCacheUtils.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kMenuHoldMs = 700;  // X3/X4: Confirm held this long opens the menu
constexpr size_t kMaxTyped = 400;           // keyboard input, bytes

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
  if (todolist::openOnLastPage()) {
    todolist::openOnLastPage() = false;
    currentPage = 1 << 20;  // the last page (clamped on render)
  }
  return true;
}

// Turning past the last page opens the next list, turning back before the first page the
// previous one (on its last page). Lists are in name order; the first and the last list stop there.
bool ListReaderActivity::openNeighbourList(const bool forward) {
  const std::vector<std::string> paths = todolist::allListPaths();
  int at = -1;
  for (size_t i = 0; i < paths.size(); i++) {
    if (todolist::detail::foldKey(paths[i]) == todolist::detail::foldKey(bookPath)) at = static_cast<int>(i);
  }
  if (at < 0) return false;
  const int to = at + (forward ? 1 : -1);
  if (to < 0 || to >= static_cast<int>(paths.size())) return false;
  todolist::openOnLastPage() = !forward;
  activityManager.goToReader(paths[to]);
  return true;
}

// Every page of a list keeps its own tears: the pattern comes from the list name and the page
// number, so going back to a page shows the same sheet, and every list tears differently.
void ListReaderActivity::newSheet() {
  uint32_t h = 2166136261u;
  for (const char c : listName) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  paperSeed = (h ^ (static_cast<uint32_t>(currentPage) * 2654435761u)) | 1u;
}

bool ListReaderActivity::save() {
  if (todolist::saveList(bookPath, items)) return true;
  message = tr(STR_TODO_SAVE_FAILED);
  return false;
}

bool ListReaderActivity::pageTurn(const bool isForward) {
  RenderLock lock(*this);
  const int last = static_cast<int>(pages.size()) - 1;
  if (isForward ? currentPage >= last : currentPage <= 0) return false;
  currentPage += isForward ? 1 : -1;
  selected = 0;
  undoPending = false;
  overlay = Overlay::None;
  return true;
}

int ListReaderActivity::itemOfEntry(const int pageEntry) const {
  if (currentPage < 0 || currentPage >= static_cast<int>(pages.size())) return -1;
  const auto& entries = pages[currentPage].entries;
  if (pageEntry < 0 || pageEntry >= static_cast<int>(entries.size())) return -1;
  return entries[pageEntry].item;
}

int ListReaderActivity::selectedEntry() const {
  if (currentPage >= static_cast<int>(pages.size())) return -1;
  const int count = static_cast<int>(pages[currentPage].entries.size());
  return selected < 0 ? count - 1 : std::min(selected, count - 1);
}

void ListReaderActivity::toggleEntry(const int pageEntry) {
  const int index = itemOfEntry(pageEntry);
  if (index < 0) return;
  items[index].done = !items[index].done;
  undoPending = false;
  message = nullptr;
  if (!save()) items[index].done = !items[index].done;  // the screen always shows what is on the card
}

void ListReaderActivity::toggleHideDone() {
  hideDone = !hideDone;
  todolist::setDoneHidden(listName, hideDone);
  currentPage = 0;
  selected = 0;
  undoPending = false;
  // the sheet keeps its tears: they only change when the reader turns a page
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

// Runs a button or menu action. Called with the render lock held.
void ListReaderActivity::runAction(const Action action, const int pageEntry) {
  overlay = Overlay::None;
  message = nullptr;
  const int index = itemOfEntry(pageEntry);
  switch (action) {
    case Action::Delete:
      if (index < 0) break;
      undoItem = items[index];
      undoIndex = index;
      items.erase(items.begin() + index);
      if (save()) {
        undoPending = true;
      } else {
        items.insert(items.begin() + index, undoItem);
      }
      break;
    case Action::Undo: {
      if (!undoPending) break;
      const int at = std::min(undoIndex, static_cast<int>(items.size()));
      items.insert(items.begin() + at, undoItem);
      if (!save()) items.erase(items.begin() + at);
      undoPending = false;
      break;
    }
    case Action::ToggleHide:
      toggleHideDone();
      break;
    case Action::Edit:
      if (index >= 0) openKeyboard(Action::Edit, index);
      break;
    case Action::Add:
    case Action::NewList:
    case Action::Rename:
      openKeyboard(action, -1);
      break;
    case Action::DeleteList:
      confirmDeleteList();
      break;
    case Action::None:
      break;
  }
}

// Deleting a whole list cannot be undone, so it asks first. On "yes" the file goes, together with
// everything kept about it (cache and home-screen picture, "recent" entry, hide setting), and the
// reader returns to the home screen.
void ListReaderActivity::confirmDeleteList() {
  overlay = Overlay::None;
  undoPending = false;
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODO_DELETE_LIST_ASK),
                                                              todolist::toHeadline(listName));
  if (!confirmation) return;
  // The result handler runs with the render lock already held: never lock again in here.
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    pagesUntilFullRefresh = 1;
    if (result.isCancelled) return;
    const std::string tmp = bookPath + ".tmp";
    if (Storage.exists(tmp.c_str())) Storage.remove(tmp.c_str());
    if (!removeBookFile(bookPath) && Storage.exists(bookPath.c_str())) {
      message = tr(STR_TODO_SAVE_FAILED);
      return;
    }
    const std::string cover = todolist::namedCoverPath(bookPath);
    if (Storage.exists(cover.c_str())) Storage.remove(cover.c_str());
    if (RECENT_BOOKS.removeByPath(bookPath)) RECENT_BOOKS.saveToFile();
    todolist::setDoneHidden(listName, false);  // also forgets lists that no longer exist
    todolist::ensureStarterList();              // was it the last one? then a fresh "TO DO"
    library::markLibraryIndexDirty();
    activityManager.goHome();
  });
}

void ListReaderActivity::openKeyboard(const Action action, const int item) {
  std::string title;
  std::string text;
  switch (action) {
    case Action::Add:
      title = tr(STR_TODO_KB_ADD);
      break;
    case Action::Edit:
      title = tr(STR_TODO_KB_EDIT);
      text = items[item].text;
      break;
    case Action::NewList:
      title = tr(STR_TODO_KB_NEW);
      break;
    case Action::Rename:
      title = tr(STR_TODO_KB_RENAME);
      text = todolist::toHeadline(listName);
      break;
    default:
      return;
  }
  undoPending = false;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, text, kMaxTyped);
  if (!keyboard) return;
  // List names are written in capitals (like the headline): the keyboard stays in upper case.
  if (action == Action::NewList || action == Action::Rename) keyboard->setCapsLock();
  keyboard->hideTips();  // the keyboard's own tips name keys these readers do not all have
  // The result handler runs with the render lock already held: never lock again in here.
  startActivityForResult(std::move(keyboard), [this, action, item](const ActivityResult& result) {
    pagesUntilFullRefresh = 1;  // clean refresh after the keyboard
    if (result.isCancelled) return;
    const std::string typed = std::get<KeyboardResult>(result.data).text;
    switch (action) {
      case Action::Add: {
        todolist::merge(items, todolist::commasToLines(typed));
        save();
        currentPage = 1 << 20;  // show the last page, where the new items are (clamped on render)
        break;
      }
      case Action::Edit: {
        if (item < 0 || item >= static_cast<int>(items.size())) break;
        const std::vector<todolist::Item> before = items;
        if (!todolist::applyEdit(items, static_cast<size_t>(item), typed)) {  // emptied: that is a delete
          undoItem = items[item];
          undoIndex = item;
          items.erase(items.begin() + item);
          undoPending = save();
          if (!undoPending) items = before;
          break;
        }
        if (!save()) items = before;
        break;
      }
      case Action::NewList: {
        const std::string name = todolist::sanitizeName(todolist::toHeadline(typed));
        if (name.empty() || !todolist::createList(name)) {
          message = tr(STR_TODO_NAME_INVALID);
          break;
        }
        activityManager.goToReader(todolist::pathFor(name));
        break;
      }
      case Action::Rename: {
        const std::string name = todolist::sanitizeName(todolist::toHeadline(typed));
        if (name.empty() || name == listName) break;
        if (!todolist::renameList(listName, name)) {
          message = tr(STR_TODO_NAME_TAKEN);
          break;
        }
        RECENT_BOOKS.removeByPath(bookPath);
        activityManager.goToReader(todolist::pathFor(name));
        break;
      }
      default:
        break;
    }
  });
}

// Overlay taps (touch) and menu keys (X3/X4). Returns true when the input was used.
bool ListReaderActivity::handleOverlayInput(const bool touch) {
  if (overlay == Overlay::None) return false;
  if (touch) {
    int x = 0;
    int y = 0;
    if (!mappedInput.wasScreenTapped(x, y)) {
      // a swipe or the back key just closes the buttons
      if (mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None ||
          mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        RenderLock lock(*this);
        overlay = Overlay::None;
        requestUpdate();
        return true;
      }
      return false;
    }
    {
      RenderLock lock(*this);
      Action hit = Action::None;
      for (const auto& b : buttons) {
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) hit = b.action;
      }
      if (hit == Action::None) {
        overlay = Overlay::None;  // tap elsewhere: close without doing anything
      } else {
        runAction(hit, overlayEntry);
      }
    }
    haptic_feedback::touchAction();
    requestUpdate();
    return true;
  }

  // X3/X4 menu
  const bool swap = mappedInput.isNavDirectionSwapped();
  const bool next =
      mappedInput.wasPressed(swap ? MappedInputManager::Button::Left : MappedInputManager::Button::Right) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward);
  const bool prev =
      mappedInput.wasPressed(swap ? MappedInputManager::Button::Right : MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageBack);
  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back);
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (!next && !prev && !back && !confirm) return true;  // the menu owns the keys while it is open
  {
    RenderLock lock(*this);
    const int count = static_cast<int>(buttons.size());
    if (back) {
      overlay = Overlay::None;
    } else if (confirm) {
      if (menuSelected >= 0 && menuSelected < count) runAction(buttons[menuSelected].action, selectedEntry());
    } else if (count > 0) {
      menuSelected = (menuSelected + (next ? 1 : count - 1)) % count;
    }
  }
  requestUpdate();
  return true;
}

void ListReaderActivity::loop() {
  rememberBookOnceRendered();
  const bool touch = mappedInput.hasTouch();
  if (handleOverlayInput(touch)) return;
  if (handleBackNavigation()) return;

  if (touch) {
    int x = 0;
    int y = 0;
    // Long press: buttons for the item (edit / delete) or for the headline (new list / rename).
    if (mappedInput.wasScreenLongPress(x, y)) {
      bool opened = false;
      {
        RenderLock lock(*this);
        if (y < paper.firstRuleY) {
          overlay = Overlay::Title;
        } else if (currentPage < static_cast<int>(pages.size()) && y < paper.footerRuleY) {
          const int hit = todolist::entryAt(pages[currentPage], y - paper.listTop, 0);
          if (hit >= 0) {
            overlay = Overlay::Item;
            overlayEntry = hit;
          }
        }
        opened = overlay != Overlay::None;
      }
      if (opened) {
        haptic_feedback::touchAction(true);
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasScreenTapped(x, y)) {
      bool used = false;
      {
        RenderLock lock(*this);
        const bool inList = y >= paper.listTop && y < paper.footerRuleY - 6;
        const int hit = currentPage < static_cast<int>(pages.size())
                            ? todolist::entryAt(pages[currentPage], y - paper.listTop, 0)
                            : -1;
        if (y < paper.firstRuleY - 4) {
          overlay = Overlay::Title;  // the headline: new list / rename / delete list
          used = true;
        } else if (inList && x < paper.marginX) {
          if (hit >= 0) toggleEntry(hit);  // box column: tick
          used = true;
        } else if (inList && hit < 0) {
          runAction(Action::Add, -1);  // an empty line: write new items there
          used = true;
        } else if (y >= paper.footerRuleY - 10) {
          // footer: undo, or hide/show when no menu gesture is set
          if (undoPending) {
            runAction(Action::Undo, -1);
            used = true;
          } else if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_OFF) {
            toggleHideDone();
            used = true;
          }
        }
      }
      if (used) {
        haptic_feedback::touchAction();
        requestUpdate();
        return;
      }
    }
    if (ReaderUtils::isTouchMenuGesture(renderer, mappedInput) ||
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
    // X3/X4: Confirm held = menu, Confirm short = tick, Left/Right = select.
    if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kMenuHoldMs)) {
      {
        RenderLock lock(*this);
        overlay = Overlay::Menu;
        menuSelected = 0;
      }
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      {
        RenderLock lock(*this);
        if (pages.empty()) {
          runAction(Action::Add, -1);  // empty list: Confirm writes the first items
        } else {
          toggleEntry(selectedEntry());
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
    // On X3/X4 the front Left/Right keys select items, so only the side keys turn pages.
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
  if (!changed && overlay == Overlay::None && openNeighbourList(!prev)) {
    if (touchTurn.prev || touchTurn.next) haptic_feedback::touchAction();
    return;
  }
  notePageTurn(!prev, changed);
  if (changed && (touchTurn.prev || touchTurn.next)) haptic_feedback::touchAction();
  if (changed) requestUpdate();
}

// Buttons for an item or the headline (touch), or the menu (keys). Called from renderBook().
void ListReaderActivity::drawOverlay(const bool touch) {
  buttons.clear();
  if (overlay == Overlay::None) return;
  const int smallAscender = renderer.getFontAscenderSize(kSmallFontId);
  const int smallLine = renderer.getLineHeight(kSmallFontId);

  // A black pill with white handwriting (selected / touch), or an outlined one (menu rows).
  const auto pill = [&](const int x, const int y, const int w, const int h, const char* text, const Action action,
                        const bool filled) {
    renderer.fillRect(x - 3, y - 3, w + 6, h + 6, false);  // white rim, so it stands out on the lines
    const int textX = x + (w - renderer.getTextWidth(kSmallFontId, text)) / 2;
    const int textY = y + (h - smallAscender) / 2 - 1;
    if (filled) {
      renderer.fillRoundedRect(x, y, w, h, h / 2, Color::Black);
      renderer.drawText(kSmallFontId, textX, textY, text, false);
    } else {
      renderer.drawRoundedRect(x, y, w, h, 2, h / 2, true);
      renderer.drawText(kSmallFontId, textX, textY, text, true);
    }
    buttons.push_back(Button{x, y, w, h, action});
  };

  if (touch && (overlay == Overlay::Item || overlay == Overlay::Title)) {
    const bool item = overlay == Overlay::Item;
    std::vector<std::pair<const char*, Action>> pills;
    if (item) {
      pills = {{tr(STR_TODO_EDIT), Action::Edit}, {tr(STR_TODO_DELETE), Action::Delete}};
    } else {
      pills = {{tr(STR_TODO_NEW_LIST), Action::NewList},
               {tr(STR_TODO_RENAME), Action::Rename},
               {tr(STR_TODO_DELETE_LIST), Action::DeleteList}};
    }
    const int h = std::max(smallLine + 14, paper.pitch - 18);
    constexpr int gap = 12;
    // Right-aligned on the line; less padding if three buttons would not fit beside the margin.
    const int room = paper.right - (paper.marginX + 6);
    int pad = 36;
    const auto total = [&](const int p) {
      int sum = gap * (static_cast<int>(pills.size()) - 1);
      for (const auto& pl : pills) sum += renderer.getTextWidth(kSmallFontId, pl.first) + p;
      return sum;
    };
    while (pad > 16 && total(pad) > room) pad -= 4;
    int y = paper.firstRuleY - h - 6;  // headline row
    if (item && currentPage < static_cast<int>(pages.size()) &&
        overlayEntry < static_cast<int>(pages[currentPage].entries.size())) {
      const int line = pages[currentPage].entries[overlayEntry].top / paper.pitch;
      y = listpaper::ruleY(paper, line) - h - 2;
    }
    int x = paper.right - total(pad);
    for (const auto& pl : pills) {
      const int w = renderer.getTextWidth(kSmallFontId, pl.first) + pad;
      pill(x, y, w, h, pl.first, pl.second, true);
      x += w + gap;
    }
    return;
  }

  // X3/X4 menu: a white card in the middle with one row per action.
  std::vector<std::pair<const char*, Action>> rows;
  if (undoPending) rows.emplace_back(tr(STR_TODO_UNDO), Action::Undo);
  rows.emplace_back(tr(STR_TODO_ADD_ITEMS), Action::Add);
  if (!pages.empty()) {
    rows.emplace_back(tr(STR_TODO_EDIT), Action::Edit);
    rows.emplace_back(tr(STR_TODO_DELETE), Action::Delete);
  }
  rows.emplace_back(hideDone ? tr(STR_TODO_MENU_SHOW) : tr(STR_TODO_MENU_HIDE), Action::ToggleHide);
  rows.emplace_back(tr(STR_TODO_NEW_LIST), Action::NewList);
  rows.emplace_back(tr(STR_TODO_RENAME), Action::Rename);
  rows.emplace_back(tr(STR_TODO_DELETE_LIST), Action::DeleteList);
  const int rowH = smallLine + 18;
  const int cardW = std::min(renderer.getScreenWidth() - 60, 300);
  const int cardH = static_cast<int>(rows.size()) * (rowH + 8) + 24;
  const int cx = (renderer.getScreenWidth() - cardW) / 2;
  const int cy = (renderer.getScreenHeight() - cardH) / 2;
  renderer.fillRect(cx, cy, cardW, cardH, false);
  renderer.drawRoundedRect(cx, cy, cardW, cardH, 3, 14, true);
  if (menuSelected >= static_cast<int>(rows.size())) menuSelected = 0;
  for (int i = 0; i < static_cast<int>(rows.size()); i++) {
    pill(cx + 16, cy + 12 + i * (rowH + 8), cardW - 32, rowH, rows[i].first, rows[i].second, i == menuSelected);
  }
}

// The list's own home-screen cover: the notepad page with the list name as heading. Made once,
// the first time the list is shown (a new or renamed list gets it right away). Drawn into the
// screen memory before the page itself, so nothing of it is ever visible.
void ListReaderActivity::ensureNamedCover() {
  if (coverChecked) return;
  coverChecked = true;
  const std::string path = todolist::namedCoverPath(bookPath);
  if (Storage.exists(path.c_str())) return;
  const auto orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.clearScreen();
  todolist::covermaker::drawBase(renderer, todolist::coverIndexFor(bookPath));

  const int* box = todolist::kTitleBox;
  const int boxWidth = box[2] - box[0] - 8;
  const int font = kTitleFontId;  // always the big writing; a long name ends with "..."
  const std::string title = renderer.truncatedText(font, todolist::toHeadline(listName).c_str(), boxWidth);
  const int top = box[3] - 14 - renderer.getFontAscenderSize(font);
  for (int ox = 0; ox < 3; ox++) {  // felt-pen strokes, like the headline
    renderer.drawText(font, box[0] + 4 + ox, top, title.c_str(), true);
  }
  const bool saved = todolist::covermaker::savePng(renderer, path);
  renderer.setOrientation(orientation);
  renderer.clearScreen();
  if (saved) {
    todolist::clearListThumbs(bookPath);
  } else {
    LOG_ERR("TODO", "could not write the cover %s", path.c_str());
  }
}

void ListReaderActivity::renderBook() {
  ensureNamedCover();
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

  // Items sit on the ruled lines: one text line = one ruled line, no extra gaps.
  const todolist::WrapFn wrap = [&](const std::string& text, const int maxLines) {
    return renderer.wrappedText(kListFontId, text.c_str(), textWidth, maxLines);
  };
  pages = todolist::layout(items, hideDone, wrap, paper.lineCount * paper.pitch, paper.pitch, 0);
  if (currentPage >= static_cast<int>(pages.size())) currentPage = std::max(0, static_cast<int>(pages.size()) - 1);
  if (!pages.empty()) {
    const int count = static_cast<int>(pages[currentPage].entries.size());
    if (selected < 0 || selected >= count) selected = count - 1;
  }
  newSheet();  // tears belong to this page

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

  // Above the headline, small and in the middle of the page (a long name is never covered):
  // how many entries there are, how many are open and how many done:  9 total  [] 5 open  [x] 4 done
  // The done number also tells how many are hidden when checked entries are hidden.
  {
    const int openCount = todolist::countOpen(items);
    const int doneCount = static_cast<int>(items.size()) - openCount;
    char totalText[24];
    char openText[24];
    char doneText[24];
    snprintf(totalText, sizeof(totalText), "%d %s", static_cast<int>(items.size()), tr(STR_TODO_COUNT_TOTAL));
    snprintf(openText, sizeof(openText), "%d %s", openCount, tr(STR_TODO_COUNT_OPEN));
    snprintf(doneText, sizeof(doneText), "%d %s", doneCount, tr(STR_TODO_COUNT_DONE));
    const int smallAscender = renderer.getFontAscenderSize(kSmallFontId);
    const int textTop = std::max(top + 1, titleBaseline - headAscender - 9 - smallAscender);
    // Icons as tall as the digits, centred on them (digits reach 63 % of the ascender above the
    // baseline in this font, so their middle is at 31 %).
    const int icon = std::max(9, smallAscender * 60 / 100);
    const int baseline = textTop + smallAscender;
    const int iconY = baseline - smallAscender * 31 / 100 - icon / 2;
    constexpr int iconGap = 5;
    constexpr int partGap = 19;  // space between "9 total", "5 open" and "4 done"
    const int totalWidth = renderer.getTextWidth(kSmallFontId, totalText);
    const int openWidth = renderer.getTextWidth(kSmallFontId, openText);
    const int doneWidth = renderer.getTextWidth(kSmallFontId, doneText);
    const int blockWidth = 3 * (icon + iconGap) + 4 + totalWidth + openWidth + doneWidth + 2 * partGap;
    int x = (paper.paperLeft + paper.right - blockWidth) / 2;  // always the middle of the page

    // "All": two boxes stacked, a ticked one behind an empty one.
    {
      constexpr int shift = 4;
      const int bx = x + shift;
      const int by = iconY - shift;
      renderer.drawRect(bx, by, icon + 1, icon + 1, true);
      renderer.drawLine(bx + 1, by + 1, bx + icon - 1, by + icon - 1, true);  // X to the edge: clearer when small
      renderer.drawLine(bx + icon - 1, by + 1, bx + 1, by + icon - 1, true);
      renderer.fillRect(x, iconY, icon + 1, icon + 1, false);
      renderer.drawRect(x, iconY, icon + 1, icon + 1, true);
      x += icon + shift + iconGap;
    }
    renderer.drawText(kSmallFontId, x, textTop, totalText, true);
    x += totalWidth + partGap;

    const auto smallBox = [&](const int bx, const bool done) {
      renderer.drawRect(bx, iconY, icon + 1, icon + 1, true);  // same size as the stacked boxes
      if (done) {
        renderer.drawLine(bx + 2, iconY + 2, bx + icon - 2, iconY + icon - 2, true);
        renderer.drawLine(bx + icon - 2, iconY + 2, bx + 2, iconY + icon - 2, true);
      }
    };
    smallBox(x, false);
    x += icon + iconGap;
    renderer.drawText(kSmallFontId, x, textTop, openText, true);
    x += openWidth + partGap;
    smallBox(x, true);
    x += icon + iconGap;
    renderer.drawText(kSmallFontId, x, textTop, doneText, true);
  }

  // Footer, under a pen line: an eye in the box column (open = checked items shown, closed =
  // hidden) and a hint that names the gesture set in Settings; or "undo" right after a delete;
  // and the page ("1/1" too).
  listpaper::paintFooterRule(paper, px);
  const int smallAscender = renderer.getFontAscenderSize(kSmallFontId);
  const int footY = paper.footerBaseline - smallAscender;
  char page[16];
  snprintf(page, sizeof(page), "%d/%d", currentPage + 1, std::max(1, static_cast<int>(pages.size())));
  const int pageW = renderer.getTextWidth(kSmallFontId, page);
  renderer.drawText(kSmallFontId, paper.right - pageW, footY, page, true);
  const int hintWidth = std::max(40, paper.right - pageW - 16 - paper.textX);
  const int doneCount = static_cast<int>(items.size()) - todolist::countOpen(items);
  char hint[96] = "";
  if (message != nullptr) {
    snprintf(hint, sizeof(hint), "%s", message);
  } else if (undoPending) {
    snprintf(hint, sizeof(hint), "%s %s", touch ? tr(STR_TODO_HINT_HERE) : tr(STR_TODO_HINT_HOLD),
             tr(STR_TODO_TO_UNDO));
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
    snprintf(hint, sizeof(hint), "%s %s", gesture, hideDone ? tr(STR_TODO_SHOW_CHECKED) : tr(STR_TODO_HIDE_CHECKED));
  }
  if (hint[0] != '\0') {
    renderer.drawText(kSmallFontId, paper.textX, footY, renderer.truncatedText(kSmallFontId, hint, hintWidth).c_str(),
                      true);
  }

  // Items: box in the margin on the item's first line, text on the ruled lines.
  const int ascender = renderer.getFontAscenderSize(kListFontId);
  if (pages.empty()) {
    const char* msg =
        items.empty() ? (touch ? tr(STR_TODO_EMPTY_TOUCH) : tr(STR_TODO_EMPTY_KEYS)) : tr(STR_TODO_ALL_DONE);
    renderer.drawText(kListFontId, paper.textX, listpaper::ruleY(paper, 0) - 4 - ascender,
                      renderer.truncatedText(kListFontId, msg, textWidth).c_str(), true);
  } else {
    const auto& pageData = pages[currentPage];
    for (int i = 0; i < static_cast<int>(pageData.entries.size()); i++) {
      const auto& entry = pageData.entries[i];
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

  drawOverlay(touch);
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
