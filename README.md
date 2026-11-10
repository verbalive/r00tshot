# r00tshot

A lightweight screen capture tool for Windows written in C++17 with the Win32 API.

Press **Page Down** anywhere and the desktop dims, letting you drag out a region.
The selection is shown with a gray control box that carries the drawing tools.
Capture to the clipboard or save a PNG without ever leaving the keyboard.

## Features

- Global capture hotkey (`Page Down`), works from any application.
- Full screen dims on capture so the selection stands out.
- Gray control box that appears only after an area is selected.
- Selective capture: only the region you drag is copied or saved.
- Drawing tools: arrow, rectangle, highlighter, pen.
- Pixel tools: blur and pixelate a rectangular region.
- Copy to the clipboard or save as PNG.
- Undo, and full keyboard shortcuts.
- Lives in the notification area with a small menu.
- Runs from a single executable, no runtime to install.
- Installs per user, no administrator rights required.

## Usage

1. Run `r00tshot.exe`. It moves to the notification area.
2. Press **Page Down** to start a capture.
3. Drag to select an area. The control box appears once a selection exists.
4. Use the tools and colors, then **Copy** or **Save**.
5. Press **Esc** or right click to step back, **Esc** again to close.

Click the notification area icon for a small menu: capture now, open the
screenshot folder, or exit.

### Tools

| Tool        | Key | Description                          |
| ----------- | --- | ------------------------------------ |
| Select      | 1   | Crop the region to capture           |
| Arrow       | 2   | Draw a straight arrow                |
| Blur        | 3   | Blur a rectangular region            |
| Rectangle   | 4   | Draw a rectangle outline             |
| Highlighter | 5   | Draw a thick highlighter stroke      |
| Pen         | 6   | Draw a freehand stroke               |
| Pixelate    | 7   | Pixelate a rectangular region        |

### Keyboard shortcuts

| Shortcut             | Action                          |
| -------------------- | ------------------------------- |
| Page Down            | Start a capture                 |
| 1 - 7                | Select a tool                   |
| Ctrl + C             | Copy the selection              |
| Ctrl + S, Enter      | Save the selection as PNG       |
| Ctrl + Z             | Undo                            |
| Esc                  | Cancel the drag, clear, or close |
| Right click          | Cancel the drag, or close        |

## Where screenshots go

Saved files are written to:

```
%USERPROFILE%\Pictures\r00tshot\r00tshot_YYYY-MM-DD_HH-MM-SS.png
```

If the Pictures folder cannot be resolved the executable folder is used.

## Installer

`r00tshot-setup.exe` installs the tool for the current user. It can add a
Start menu shortcut, register the program to run at logon, and start it
right away.

```
r00tshot-setup.exe                 interactive install
r00tshot-setup.exe --silent        accept every default
r00tshot-setup.exe --dir <path>    install into <path>
r00tshot-setup.exe --startup       enable the logon entry
r00tshot-setup.exe --no-startup    disable the logon entry
r00tshot-setup.exe --uninstall     remove the installation
r00tshot-setup.exe --help          show usage
```

The default install folder is `%LocalAppData%\Programs\r00tshot`. The logon
entry is written to `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

Keep `r00tshot-setup.exe` next to `r00tshot.exe` when you run it.

## Building

Requirements:

- Windows 10 or 11.
- MinGW-w64 with `g++` and `windres` available on `PATH`.
  The project is built and tested with g++ 16.2.0.

Build with the batch script:

```
build.bat
```

Output:

```
bin\r00tshot.exe          background capture tool
bin\r00tshot-setup.exe    command line installer
```

An MSVC build is also described in `CMakeLists.txt`. Configure the project
with CMake and build the `r00tshot` target.

## Project layout

```
src/common.h        shared declarations and platform setup
src/util.cpp        path and string helpers
src/capture.cpp     screen capture, pixel effects, PNG encoding, clipboard
src/main.cpp        background host: hotkey, notification area, message loop
src/overlay.cpp     full screen selection overlay and tools
src/installer.cpp   command line installer
res/r00tshot.rc     resources
res/r00tshot.manifest  common controls v6 manifest
build.bat           MinGW-w64 build script
CMakeLists.txt      CMake project for MSVC
```

## Notes

- No administrator rights are needed. Everything is stored per user.
- The overlay stays on top and keeps keyboard focus while it is open.
- Only one instance runs at a time; launching it again does nothing.
