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
1. Build: `cmake . && make` (or from the project root: `mkdir build && cd build && cmake .. && make`)
2. Run the built app: open `build/KbdLayoutOverlay.app`
3. Grant permissions if prompted:
   - On macOS 12+ you may need to enable Input Monitoring (and sometimes Accessibility) in System Settings → Privacy & Security → Input Monitoring / Accessibility for "KbdLayoutOverlay".
   - If the app cannot create the global event tap you'll see a log entry "Failed to create CGEventTap for hotkey" and the app will offer to open the Privacy settings.
   - After granting permissions, fully quit and restart the app.
4. Verify UI: look for the "KLO" status item in the menu bar.
5. Test hotkey: press and hold Cmd+Option+Shift+/ — overlay should appear while held and disappear on release.
6. Logs and troubleshooting:
   - Runtime log: /tmp/kbd_layout_overlay.log
     - Contains startup, hotkey press/release, show/hide, and crash/exit messages.
   - Position/debug files (for overlay diagnostics):
     - /tmp/kbd_overlay_debug.txt
     - /tmp/kbd_overlay_position.txt
   - If you see "Failed to create CGEventTap for hotkey" in the log, open System Settings → Privacy & Security → Input Monitoring (and Accessibility) and enable the app, then restart the app.

### Windows  
1. Build with `cmake . && cmake --build . --config Release`
2. Run `kbd_layout_overlay.exe`
3. Look for tray icon
4. Press Ctrl+Alt+Shift+/ to show overlay

## Customization (Before Building)

**To embed your own keyboard layout:**
1. Place your `keymap.png` file in the `assets/` folder
2. Build the app - it will automatically embed your PNG into the binary
3. No runtime file dependencies!

## Building

```bash
# 1. OPTIONAL: Add your keymap.png to assets/ folder first
cp your_keyboard_layout.png assets/keymap.png

# 2. Build normally 
mkdir build && cd build
cmake ..
make                    # macOS/Linux
# or
cmake --build .         # Windows
```

**What happens:**
- ✅ **keymap.png in assets/?** → Embedded in binary at build time (recommended)
- ❌ **No keymap.png?** → App tries to load external file at runtime

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
