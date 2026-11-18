// PC test for ListCore.h:  g++ -std=c++17 -Wall -Wextra -I../patch/files test_listcore.cpp && ./a.out
#include "ListCore.h"

#include <cstdio>
#include <cstdlib>

using namespace todolist;

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static void testParse() {
  auto items = parse("\xEF\xBB\xBF[x] Milch\r\n[ ] Brot\n\n   \nKaffee\r- Käse\n• Eier\n1. Butter\n2) Salz\n- [ ] Tee\n* [X] Zucker\n[ ]\n-\n");
  CHECK(items.size() == 9);
  CHECK(items[0].done && items[0].text == "Milch");
  CHECK(!items[1].done && items[1].text == "Brot");
  CHECK(!items[2].done && items[2].text == "Kaffee");
  CHECK(items[3].text == "Käse");
  CHECK(items[4].text == "Eier");
  CHECK(items[5].text == "Butter");
  CHECK(items[6].text == "Salz");
  CHECK(!items[7].done && items[7].text == "Tee");
  CHECK(items[8].done && items[8].text == "Zucker");

  // Not bullets: a minus sign with a number, a year, a bracket that is no marker.
  items = parse("-5 Grad\n2026 planen\n[a] Notiz\n");
  CHECK(items.size() == 3);
  CHECK(items[0].text == "-5 Grad");
  CHECK(items[1].text == "2026 planen");
  CHECK(items[2].text == "[a] Notiz");
}

static void testSerialize() {
  auto items = parse("Milch\n[x] Brot\n");
  CHECK(serialize(items) == "[ ] Milch\n[x] Brot\n");
  CHECK(serialize(parse(serialize(items))) == serialize(items));  // stable round trip
}

static void testMerge() {
  auto items = parse("[x] Milch\n[ ] Brot\n");
  const auto r = merge(items, "milch\nBROT\nKäse\nkäse\n");
  CHECK(r.reopened == 1);
  CHECK(r.skipped == 2);  // Brot, and the second Käse
  CHECK(r.added == 1);
  CHECK(serialize(items) == "[ ] Milch\n[ ] Brot\n[ ] Käse\n");

  // Umlauts fold for duplicates (ÄPFEL == äpfel), and incoming [x] is still added as open.
  items = parse("[ ] äpfel\n");
  const auto r2 = merge(items, "ÄPFEL\n[x] Birnen\n");
  CHECK(r2.skipped == 1 && r2.added == 1);
  CHECK(items[1].text == "Birnen" && !items[1].done);

  // Extra spaces inside an entry do not make a new one.
  items = parse("[ ] Batterien AA\n");
  CHECK(merge(items, "Batterien   AA").skipped == 1);
}

static void testNames() {
  CHECK(sanitizeName("Einkauf") == "Einkauf");
  CHECK(sanitizeName("  Ein/kauf: Wien?  ") == "Einkauf Wien");
  CHECK(sanitizeName("Einkauf.TXT") == "Einkauf");
  CHECK(sanitizeName("..versteckt") == "versteckt");
  CHECK(sanitizeName("///").empty());
  CHECK(sanitizeName(std::string(100, 'a')).size() == kMaxNameBytes);
  // A cut never splits a UTF-8 letter.
  const std::string longUml = std::string(59, 'a') + "ä";
  CHECK(sanitizeName(longUml) == std::string(59, 'a'));
  CHECK(pathFor("Einkauf") == "/Lists/Einkauf.txt");
  CHECK(isListPath("/Lists/Einkauf.txt"));
  CHECK(isListPath("/lists/Einkauf.TXT"));
  CHECK(!isListPath("/Lists/sub/Einkauf.txt"));
  CHECK(!isListPath("/Books/Einkauf.txt"));
  CHECK(!isListPath("/Lists/Einkauf.epub"));
  CHECK(!isListPath("/Lists/.txt"));
  CHECK(nameFromPath("/Lists/Urlaub packen.txt") == "Urlaub packen");
}

static void testLayout() {
  // Fake wrap: 10 characters per line.
  const WrapFn wrap = [](const std::string& text, int maxLines) {
    std::vector<std::string> lines;
    for (size_t i = 0; i < text.size() && static_cast<int>(lines.size()) < maxLines; i += 10) {
      lines.push_back(text.substr(i, 10));
    }
    return lines;
  };
  std::vector<Item> items = parse("[ ] a\n[x] b\n[ ] 0123456789abc\n[ ] d\n[ ] e\n");
  // area 100 px, line 20 px, gap 10 px: a(0-20) b(30-50) c(60-100) | d e
  auto pages = layout(items, false, wrap, 100, 20, 10);
  CHECK(pages.size() == 2);
  CHECK(pages[0].entries.size() == 3);
  CHECK(pages[0].entries[2].top == 60 && pages[0].entries[2].height == 40);
  CHECK(pages[1].entries.size() == 2 && pages[1].entries[0].top == 0);

  // Hidden done entries take no space.
  pages = layout(items, true, wrap, 100, 20, 10);
  CHECK(pages[0].entries.size() == 3);
  CHECK(pages[0].entries[1].item == 2);

  // Taps between entries belong to the nearer one; below the last entry counts too.
  pages = layout(items, false, wrap, 100, 20, 10);
  CHECK(entryAt(pages[0], 0, 10) == 0);
  CHECK(entryAt(pages[0], 24, 10) == 0);
  CHECK(entryAt(pages[0], 26, 10) == 1);
  CHECK(entryAt(pages[0], 99, 10) == 2);
  CHECK(entryAt(pages[0], 200, 10) == -1);

  // Empty list, and a list where everything is done and hidden.
  CHECK(layout({}, false, wrap, 100, 20, 10).empty());
  CHECK(layout(parse("[x] a\n"), true, wrap, 100, 20, 10).empty());
  CHECK(countOpen(items) == 4);
}

int main() {
  testParse();
  testSerialize();
  testMerge();
  testNames();
  testLayout();
  if (failures == 0) std::printf("All ListCore tests passed.\n");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
