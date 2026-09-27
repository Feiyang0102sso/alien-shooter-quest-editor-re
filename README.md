# QuestEditorRe

[简体中文](README_CN.md)

QuestEditorRe is a remake of the original QuestEditor. It can run as a standalone application or be loaded by a compatible map editor as a DLL.

## Features

- Edits `levels.cfg`, `levels.db`, and `mission.txt`.
- Supports creating and deleting quests, editing properties, linking quests, defining level boundaries, and undo/redo.
- Preserves comments, unknown fields, formatting, and unmodified bytes whenever possible.
- Automatically creates rotating backups before saving to reduce the risk of data loss.
- Provides Chinese and English interfaces and supports CP1251, GBK, and system ANSI game text.
- Retains the original QuestEditor's Win32 DLL interface for integration with compatible map editors.

## Installation and Usage

Place the following files in the game directory. `QuestEditor.exe` and `QuestEditor.dll` must be in the same directory:

```text
QuestEditor.exe
QuestEditor.dll
QuestEditor.cfg
```

You can start the editor in any of the following ways:

1. Double-click `QuestEditor.exe` to open the quest files in the current game directory.
2. Press `Q` in a compatible map editor to open the editor through `QuestEditor.dll`.
3. Pass a game directory or the path to `levels.cfg` or `levels.db` to `QuestEditor.exe`.

Before replacing the DLL, close the game, map editor, and any older QuestEditor instance, then back up the original files. If both `levels.cfg` and `levels.db` exist, the editor opens `levels.cfg` first.

## Compatibility

Current compatibility targets include:

- Alien Shooter 2
- Alien Shooter 2 Reloaded series
- Zombie Shooter 2
- Alien Shooter 2 Object Extend 1106

Different releases, localized editions, and mods may alter the quest format or host behavior. Current automated tests against a real game installation primarily target AS2R. Test the editor with a separate copy of the game before using it with other versions.

## Basic Controls

| Action | Method |
|---|---|
| View or edit a quest | Double-click a quest box, or select it and press `Enter` |
| Create a quest | Double-click an empty grid cell |
| Create a link | Drag from one quest box to another |
| Delete a link | Click the link and press `Delete` |
| Delete a quest | Select the quest box and press `Delete` |
| Select multiple items | `Ctrl` + click, or drag a selection box |
| Pan the canvas | Drag with the middle mouse button, or hold Space and drag with the left mouse button |
| Zoom the canvas | Use the mouse wheel |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` |
| Save | `Ctrl+S` |

The OK button in the properties window only applies changes to the current editing session. Save from the main window to write those changes to disk.

The bonus page has separate Give and Remove columns. Each dropdown reads item IDs from
`Weapon.cfg` beside the current quest configuration and filters by case-insensitive substring.
Selecting a dropdown item adds it directly; Enter also adds an item. Lists support copying and pasting one ID per line;
spaces and `^` are also accepted as separators, and duplicates are retained. Saving uses the
game's space-separated format. Manual entry and pasting remain available without `Weapon.cfg`.

## Language and Encoding

The interface supports Chinese and English and can be changed from the language menu. The selected language is stored in `QuestEditor.cfg`:

```ini
; Interface language: 1 = en, 2 = cn
language = 2
```

The interface language and game-file encoding are independent. Russian game text uses CP1251, while Chinese game text usually uses GBK. Pure ASCII files, or files whose encoding cannot be identified reliably, use the system ANSI code page. If text appears corrupted, select the correct CFG or mission display encoding before editing.

## Building

Build requirements:

- Windows 10/11
- Visual Studio C++ toolset `v145`
- Windows SDK 10
- C++20
- Win32 platform

Open `quest_editor_re.sln` in Visual Studio, select `Release | Win32`, and build the solution. Build output is written to:

```text
bin/Win32/Release/
```

To debug the standalone application, set `QuestEditorLauncher` as the startup project. The DLL must remain a Win32 build; an x64 DLL cannot replace the plugin loaded by the original map editor.

## Testing

Run the following command from the project root:

```bat
tests\main.bat
```

The script builds `Release | Win32` and runs the automated tests. You can also run a specific test group:

```bat
tests\main.bat unit
tests\main.bat in_game
tests\main.bat in_editor
```

For detailed instructions, test coverage, and important notes, see [tests/README.md](tests/README.md).

## Data Safety

- Before saving, the editor creates rotating `.qere.1.bak` and `.qere.2.bak` backups.
- Editor layout is stored in `.qere-layout`; the game does not read this file.
- After changing quest links, timelines, or level boundaries, verify the resulting behavior in the game.
