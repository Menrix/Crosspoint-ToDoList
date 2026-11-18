# CrossPoint To-Do List

A small add-on for [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader):
to-do and shopping lists you can tick off on the reader, and add to from your phone or computer
with the normal **Share** menu.

This repository does not contain a copy of CrossPoint. A GitHub Action takes every new official
CrossPoint release, adds the lists and publishes the finished firmware under **Releases**.

| Device | File |
| --- | --- |
| Xteink X4 Pro | `crosspoint-<version>-x4pro.bin` |
| Xteink X3 and X4 (one file for both) | `crosspoint-<version>-x3-x4.bin` |

> **X3 and X4: not tested on a device.** Only the X4 Pro is tested. The X3 and X4 have no touch
> screen, so everything runs over the few buttons (and their layout differs by setting), which
> makes the lists harder to use there. Feedback welcome.

## How it works

Every list is a plain text file in the folder **`/Lists`** on the SD card, one entry per line:

```
[x] Milk
[ ] Bread
[ ] AA batteries, the big pack
```

`[ ]` is open, `[x]` is done. The tick is stored in the file itself, so any text editor can read
and write the lists. Lines without `[ ]` count as open entries.

Open a list from the file browser or the library like a book. Other books are not affected.

The list looks like a sheet torn from a spiral notepad, written in a handwriting font
([Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand), built in, with ä ö ü ß and the usual
special characters). Every page is torn a little differently. On the home screen, every list shows as a
handwritten notepad page. There are five slightly different pages; each list always gets the same
one (picked from its name), so lists next to each other look different. The reader keeps the
pictures in `/.crosspoint/todolist/` and writes them again if they are missing.

### X4 Pro (touch screen)

| Action | Result |
| --- | --- |
| **Tap the margin on the left** (left of the margin line, where the boxes are) | Ticks or unticks that entry |
| Swipe, or tap elsewhere | Turns the page, exactly as set in Settings |
| **Reader-menu gesture** (as set in Settings: tap the middle or swipe up) | Hides or shows checked items for this list |
| **Tap an empty line** | Opens the keyboard to add entries |
| **Hold an entry** | Edit or delete it (delete has an "undo" in the bottom line) |
| **Hold the headline** | New list, rename this list, add entries |

On the keyboard, `,` or `;` separate entries: `Milk, Bread; Cheese` adds three. When editing,
the first part replaces the entry and the others are added right below it. Emptying the text
deletes the entry.

### X3 and X4 (buttons only, not tested)

| Key | Result |
| --- | --- |
| **Left / Right** (front) | Moves the marker to the previous / next entry, across pages |
| **Confirm** | Ticks or unticks the marked entry |
| **Confirm, hold** | Menu: hide/show checked, add, edit, delete, new list, rename |
| Side keys | Turn the page |

The bottom line of every page says how: an open eye with "swipe up to hide checked"
(the gesture follows your Settings), or a closed eye with "... to show checked" while they
are hidden. With the reader-menu gesture switched off, tapping that line works instead. A Home key
set to "Reader menu" works too. The choice is remembered per list; hidden items stay in the file.

To delete a list, delete its file (file browser: hold Confirm; library on touch: swipe the row).

## Add entries from your phone or computer

On the reader, start **File Transfer** (at home in your Wi-Fi, or **Create Hotspot** on the go).
Then send text to the reader. New entries are always **added** to a list, never replacing it:

- a new entry is appended at the end,
- an entry that is already open is skipped (no duplicates),
- an entry that is already done is opened again ("milk" is back on the shopping list).

Upper and lower case do not matter. Empty lines and bullets (`-`, `*`, `•`, `1.`) are removed,
so text from any notes app works. Up to 32 KB of text per send (about 1,000 short entries).

The reader's address is `http://crosspoint.local`. If your phone cannot find it, use the IP
address the reader shows on the File Transfer screen instead.

### iPhone, iPad and Mac (Shortcuts app)

Create a shortcut once; it then appears in the **Share** menu of every app.

1. New shortcut, name it **To X4**. In its details turn on **Show in Share Sheet**.
2. Set it to receive **Text** from the Share Sheet; *If there's no input:* **Ask For** Text.
3. **Get Contents of URL** `http://crosspoint.local/api/lists`
4. **Get Dictionary Value** for `lists`, then **Get Dictionary Value** for `name` (gives all list names).
5. **Add** the text `New list` to that list of names, then **Choose from List**.
6. **If** the chosen item is `New list`: **Ask for Input** "List name". Otherwise: use the chosen item.
   Save the result in a variable **List**.
7. **Get Contents of URL** `http://crosspoint.local/api/lists/add`, Method **POST**,
   Request Body **Form**: `list` = *List*, `text` = *Shortcut Input*.
8. **Get Dictionary Value** for `message`, then **Show Notification**.

Use it: select text in Notes (or anywhere) → **Share** → **To X4** → pick the list.

### Android (free app HTTP Shortcuts)

Install [HTTP Shortcuts](https://http-shortcuts.rmy.ch) (Play Store or F-Droid), then:

1. Variables: add **text** (type *Text input*, multi-line, **Allow receiving value from Share dialog** on)
   and **list** (type *Text input*, or *Select* with your list names).
2. New shortcut **To X4**: Method **POST**, URL `http://crosspoint.local/api/lists/add`.
3. Request body: **Form parameters**, `list` = `{list}`, `text` = `{text}`.
4. Response handling: show as toast.

Use it: select text → **Share** → **To X4**.

### Mac or PC without Shortcuts

- **Finder (WebDAV):** Go → Connect to Server → `http://crosspoint.local`, then save `.txt` files into `/Lists`.
- **Terminal:** `curl -d "list=Shopping" --data-urlencode "text=Milk" http://crosspoint.local/api/lists/add`

### Web addresses

| Address | What it does |
| --- | --- |
| `GET /api/lists` | All lists: `{"lists":[{"name":"Shopping","open":3,"total":5}]}` |
| `POST /api/lists/add` | Form fields `list` and `text`. Answer: `{"list","added","reopened","skipped","message"}` |

Like all of CrossPoint's File Transfer, these addresses have no password. Use them only in your
own Wi-Fi or in the reader's own hotspot. They only ever write files in `/Lists`.

## Install

1. Open the latest release and download the file for your device (see the table above).
2. Install it, either way works:
   - **SD card:** copy the file to the root of the SD card, then on the reader go to
     **Settings → SD Card Firmware Update** and pick the file.
   - **USB:** go to <https://crosspointreader.com/#flash-tools> in Chrome or Edge, choose your device,
     then **Custom .bin**, and upload the file.

After that, **Settings → Check for Updates** on the reader installs new versions from this
repository, so updates keep the lists.

## Updates and maintenance

- The Action runs every night. When CrossPoint publishes a new release, a matching release appears here.
- To build by hand: **Actions → Build CrossPoint with to-do lists → Run workflow**
  (tick **force** to rebuild a version that already exists).
- If CrossPoint changes the code the patch hooks into, the build stops with a message that
  names the spot (`TODOLIST PATCH FAILED: ...`). GitHub sends an email; then
  `patch/apply_todolist.py` needs an update.

## Files

- `patch/files/ListCore.h`: the list rules (reading, saving, adding, page layout), no hardware code
- `patch/files/ListStore.h`: reads and writes the list files on the SD card
- `patch/files/ListPaper.h`: the notepad look (no hardware code; `tools/` draws a PC preview with it)
- `patch/files/ListPath.h`: which files are lists and which cover each one gets (also copied to `lib/Txt`)
- `patch/files/ListCover.h`, `ListCoverImages.h`: the five notepad covers
- `tools/cover/`: the source picture and the script that makes the covers from it
- `patch/files/fonts/`: the handwriting font, converted with CrossPoint's own font tool
- `patch/files/ListReaderActivity.*`: the list screen on the reader
- `patch/files/ListsWeb.h`: the two web addresses
- `patch/apply_todolist.py`: adds all of this to a CrossPoint source tree
- `tests/test_listcore.cpp`: tests for the list rules, run on a PC before every build
- `.github/workflows/build.yml`: nightly build and release for both devices

Self-built firmware does not include the official release key for CrossPoint's LCP service
(only relevant for LCP-protected books from that service). Everything else works as in the
official release.

MIT licensed, like CrossPoint. The Patrick Hand font is © Patrick Wagesreiter, licensed under the
SIL Open Font License 1.1 (`licenses/PatrickHand-OFL.txt`).
