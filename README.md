# CrossPoint To-Do List

To-do and shopping lists for the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
firmware: tick them off on the reader, write them on the reader, or send entries from your phone
or computer.

![A shopping list on the Xteink X4 Pro](images/list.jpg)

- Looks like a page torn from a spiral notepad, in a handwriting font
- Tap the box to tick an entry, hide checked entries per list
- Add, edit and delete entries and lists right on the reader
- No double entries: an entry that is already on the list is not added again, whatever the
  upper and lower case (a done one is simply opened again)
- Lists are plain text files, so any text editor works too
- On the home screen every list shows as a handwritten notepad page

## Install

Download the file for your device from **[Releases](../../releases)**:

| Device | File |
| --- | --- |
| Xteink X4 Pro | `crosspoint-<version>-x4pro.bin` |
| Xteink X3 and X4 | `crosspoint-<version>-x3-x4.bin` |

> **X3 and X4: not tested on a device.** Only the X4 Pro is tested. The X3 and X4 have no touch
> screen, so everything runs over the few buttons, which makes the lists harder to use there.
> Feedback welcome.

Then either copy it to the SD card root and use **Settings → SD Card Firmware Update**, or flash it
over USB at <https://crosspointreader.com/#flash-tools> (Chrome or Edge, **Custom .bin**).
Later, **Settings → Check for Updates** gets new versions from here.

## Use

Open a list from the file browser or the library, like a book. A first list, **TO DO**, is
already there. List names are written in capitals, like the headline.

| What | X4 Pro (touch) | X3 / X4 (buttons) |
| --- | --- | --- |
| Tick / untick | Tap the box margin on the left | Left / Right to pick, **Confirm** |
| Turn the page | Swipe or tap elsewhere (as in Settings) | Side keys |
| Add entries | Tap an empty line | **Confirm** held → menu |
| Edit or delete an entry | Hold the entry | **Confirm** held → menu |
| New list, rename, delete list | Tap the headline | **Confirm** held → menu |
| Hide / show checked | Reader-menu gesture (the bottom line says which) | **Confirm** held → menu |

- **Several entries at once:** separate them with `,` or `;`, e.g. `Milk, Bread; Cheese`.
  When editing, the first part replaces the entry and the rest go right below it.
- **Delete:** empty the text, or choose delete. The bottom line offers **undo**.
- **Delete a whole list:** tap the headline → **delete list**. It asks first, then removes the
  list with its cover and returns to the home screen.

## Lists on the SD card

Each list is a text file in **`/Lists`**, one entry per line. `[ ]` is open, `[x]` is done:

```
[x] Milk
[ ] Bread
```

Lines without `[ ]` count as open. Bullets (`-`, `*`, `•`, `1.`) and empty lines are tidied up.

## Send entries from a phone or computer

Start **File Transfer** on the reader (home Wi-Fi, or **Create Hotspot** on the go). Sent entries are
always **added**: duplicates are skipped, done ones are opened again. Up to 32 KB per send.

- **Upload a file:** put a `.txt` into `/Lists` with File Transfer in the browser
  (this replaces that list).
- **Terminal:** `curl -d "list=Shopping" --data-urlencode "text=Milk, Bread" http://crosspoint.local/api/lists/add`

If `crosspoint.local` does not work, use the IP address shown on the reader.

<details>
<summary>Share menu on iPhone, iPad and Mac (Shortcuts)</summary>

1. New shortcut **To X4**, turn on **Show in Share Sheet**, receive **Text** (*if no input:* **Ask For** Text).
2. **Get Contents of URL** `http://crosspoint.local/api/lists`, then **Get Dictionary Value** `lists`
   and **Get Dictionary Value** `name`.
3. **Add** `New list` to that list, **Choose from List**. If `New list` was chosen, **Ask for Input**
   "List name". Save the result as variable **List**.
4. **Get Contents of URL** `http://crosspoint.local/api/lists/add`, Method **POST**,
   Body **Form**: `list` = *List*, `text` = *Shortcut Input*.
5. **Get Dictionary Value** `message`, **Show Notification**.

</details>

<details>
<summary>Share menu on Android (HTTP Shortcuts app)</summary>

1. Install [HTTP Shortcuts](https://http-shortcuts.rmy.ch).
2. Variables: **text** (text input, multi-line, *Allow receiving value from Share dialog*) and
   **list** (text input, or *Select* with your list names).
3. Shortcut **To X4**: **POST** `http://crosspoint.local/api/lists/add`,
   form parameters `list` = `{list}`, `text` = `{text}`, show the response as toast.

</details>

<details>
<summary>Web addresses</summary>

| Address | What it does |
| --- | --- |
| `GET /api/lists` | All lists: `{"lists":[{"name":"Shopping","open":3,"total":5}]}` |
| `POST /api/lists/add` | Form fields `list`, `text`. Answer: `{"list","added","reopened","skipped","message"}` |

Like all of CrossPoint's File Transfer, there is no password: use it only in your own Wi-Fi or the
reader's hotspot. It only ever writes files in `/Lists`.

</details>

## How it is built

This repository holds only the add-on, no copy of CrossPoint. Every night a GitHub Action takes the
latest official CrossPoint release, adds the lists with `patch/apply_todolist.py`, builds both
devices and publishes them under Releases. By hand: **Actions → Build CrossPoint with to-do lists
→ Run workflow** (**force** rebuilds an existing version). If CrossPoint changed a spot the patch
needs, the build stops with `TODOLIST PATCH FAILED: ...` instead of making a broken firmware.

<details>
<summary>Files</summary>

| File | Content |
| --- | --- |
| `patch/apply_todolist.py` | Adds everything to a CrossPoint source tree |
| `patch/files/ListCore.h` | List rules: reading, saving, adding, page layout |
| `patch/files/ListStore.h` | Reading and writing lists on the SD card |
| `patch/files/ListPath.h` | Which files are lists, which cover each gets |
| `patch/files/ListPaper.h` | The notepad look |
| `patch/files/ListCover.h`, `ListCoverImages.h` | The five home-screen covers |
| `patch/files/ListReaderActivity.*` | The list screen |
| `patch/files/ListsWeb.h` | The web addresses |
| `patch/files/fonts/` | The handwriting font |
| `tests/test_listcore.cpp` | Tests, run before every build |
| `tools/` | PC preview and the cover maker |

</details>

Self-built firmware lacks the official key for CrossPoint's LCP service (only relevant for
LCP-protected books from it); everything else works as in the official release.

## License

MIT, like CrossPoint. The [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) font is
© Patrick Wagesreiter, SIL Open Font License 1.1 (`licenses/PatrickHand-OFL.txt`).
