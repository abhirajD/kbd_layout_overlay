# Keyboard Layout Overlay

A lightweight utility that shows a keyboard overlay when you press a hotkey. Simple, fast, no bullshit.

## Features

- **Single hotkey:** Show/hide overlay with Cmd+Option+Shift+/ (macOS) or Ctrl+Alt+Shift+/ (Windows)
- **System tray:** Right-click for options
- **Minimal UI:** Persistent mode toggle, color invert, quit
- **Auto-fallback:** Uses built-in overlay if keymap.png not found
- **Cross-platform:** Works on macOS and Windows

## Installation

### macOS
1. Build with `cmake . && make`
2. Run `KbdLayoutOverlay.app`
3. Look for "KLO" in menu bar
4. Press Cmd+Option+Shift+/ to show overlay

### Windows  
1. Build with `cmake . && cmake --build . --config Release`
2. Run `kbd_layout_overlay.exe`
3. Look for tray icon
4. Press Ctrl+Alt+Shift+/ to show overlay

## Building

```bash
mkdir build && cd build
cmake ..
make                    # macOS/Linux
# or
cmake --build .         # Windows
```

## Customization

Drop a `keymap.png` file next to the executable to use your own keyboard layout.

## Architecture

**Clean and simple:**
- `shared/` - Core overlay loading (105 lines)
- `windows/` - Win32 implementation (229 lines)  
- `macos/` - Cocoa implementation (234 lines)
- **Total:** ~570 lines of actual code

**What we removed:**
- Config file parsing (just use hardcoded defaults)
- Multi-monitor support (primary only)
- Autostart registration
- Async caching and threading
- Elaborate error handling
- Platform abstraction layers

This proves you don't need 2000+ lines of "enterprise" code for a simple overlay app.
