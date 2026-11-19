# CrossPoint To-Do List

To-do and shopping lists for the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
firmware.

![A shopping list on the Xteink X4 Pro](images/list.jpg)

- Notepad look in a handwriting font
- Tick, add, edit and delete entries and lists right on the reader
- Hide checked entries per list
- Send entries from your phone or computer
- No double entries (upper or lower case doesn't matter)
- Lists are plain text files

## Install

Get the file for your device from **[Releases](../../releases)**:

| Device | File |
| --- | --- |
| Xteink X4 Pro | `crosspoint-<version>-x4pro.bin` |
| Xteink X3 and X4 | `crosspoint-<version>-x3-x4.bin` |

> **X3 and X4: not tested.** No touch screen there, so everything runs over the buttons, which
> makes the lists harder to use.

Copy it to the SD card and use **Settings → SD Card Firmware Update**. Later updates:
**Settings → Check for Updates**.

## Use

Open a list like a book. A first list, **TO DO**, is already there.

| What | X4 Pro (touch) | X3 / X4 (buttons) |
| --- | --- | --- |
| Tick | Tap the box | Left / Right, **Confirm** |
| Add entries | Tap an empty line | Hold **Confirm** → menu |
| Edit or delete an entry | Hold the entry | Hold **Confirm** → menu |
| New list, rename, delete list | Tap the headline | Hold **Confirm** → menu |
| Hide / show checked | Reader-menu gesture | Hold **Confirm** → menu |

- `,` or `;` separate entries: `Milk, Bread; Cheese` is three. For a comma inside an entry,
  use a dash: `Apples - the red ones`.
- A deleted entry can be brought back with **undo** in the bottom line. Deleting a whole list
  asks first.

## Lists on the SD card

Each list is a text file in `/Lists`, one entry per line, `[ ]` open, `[x]` done:

```
[x] Milk
[ ] Bread
```

Plain lines (also with `-`, `*` or `1.` in front) work too and become open entries.
The order always stays as written.

## Send from a phone or computer

Start **File Transfer** on the reader, then either upload a `.txt` into `/Lists` in the
browser, or send entries (they are added to the list; use the reader's IP if
`crosspoint.local` doesn't work):

```
curl -d "list=Shopping" --data-urlencode "text=Milk, Bread" http://crosspoint.local/api/lists/add
```

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

Every night a GitHub Action adds this to the latest official CrossPoint release and publishes
the firmware under Releases.

Made with the help of Claude.

## License

MIT, like CrossPoint. Font [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand)
© Patrick Wagesreiter, SIL Open Font License 1.1.
