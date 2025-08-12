# kbd_layout_overlay

A lightweight utility that displays an image overlay in response to a global hotkey. Native implementations are provided for macOS and Windows.

## Installation

### Windows
1. Download `kbd_layout_overlay.exe` from the releases page or build it from source (see [Contributor Guide](#contributor-guide)).
2. Place the executable and an optional `keymap.png` in a directory of your choice.
3. Run the executable once; a `config.cfg` file is created next to it.

### macOS
1. Download `Kbd Layout Overlay.app` from the releases page or build it from source.
2. Move the app to `Applications` and launch it.
3. A configuration file is written to `~/Library/Preferences/kbd_layout_overlay.cfg`.

## Configuration

Settings are stored in a simple `key=value` file.

| option        | description |
|---------------|-------------|
| `overlay_path`| Path to a custom image. If empty, the program looks for a `keymap.png` next to the executable and falls back to a bundled default if none is found. |
| `opacity`     | Overlay opacity from `0.0`–`1.0`. |
| `invert`      | `1` inverts colors, `0` leaves them unchanged. |
| `autostart`   | `1` launches the app at login, `0` disables autostart. |
| `hotkey`      | `+` separated list of modifiers and key, e.g. `Ctrl+Alt+K` or `Command+Option+K`. |
| `persistent`  | `1` toggles the overlay, `0` shows it only while keys are held. |

Edit the file with any text editor:

- **Windows:** `config.cfg` in the same directory as `kbd_layout_overlay.exe`.
- **macOS:** `~/Library/Preferences/kbd_layout_overlay.cfg`.

### Replacing the overlay image

A default `keymap.png` is embedded in the application. To use a different layout, set `overlay_path` to another image or place a file named `keymap.png` next to the executable (`kbd_layout_overlay.exe` on Windows, `Kbd Layout Overlay.app/Contents/MacOS/` on macOS). The external image takes precedence over the bundled one. Images are automatically scaled to fit the screen.

## Autostart and Hotkeys

The overlay appears when the configured shortcut is pressed.

- **Default hotkeys:** `Ctrl+Alt+Shift+Slash` on Windows, `Cmd+Option+Shift+/` on macOS.
- Change the shortcut by editing the `hotkey` entry in the configuration file.
- Enable persistent mode with `persistent=1`.

To start the application at login, set `autostart=1` in the configuration file or run:

```
kbd_layout_overlay autostart enable   # register
kbd_layout_overlay autostart disable  # unregister
```

On Windows this creates or removes a registry entry under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. On macOS a `LaunchAgents` plist is created or deleted.

## Contributor Guide

### Repository layout

```
shared/   - platform-neutral C sources and assets
windows/  - Win32 implementation and build script
macos/    - Cocoa implementation and build script
legacy/   - historical Rust version (unmaintained)
```

### Building

```
make -C shared             # build common static library
windows\build_windows.bat  # compile Windows executable (Visual Studio Developer Command Prompt)
./macos/build_macos.sh     # build macOS app bundle
```

Pass `--install` to the macOS build script to install the LaunchAgent for autostart.
