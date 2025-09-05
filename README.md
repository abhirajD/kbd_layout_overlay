# Keyboard Layout Overlay

Shows keyboard shortcuts overlay on hotkey press. Works on macOS and Windows.

## Try It

**Download pre-built binaries:**
- Go to [Actions](https://github.com/abhirajD/kbd_layout_overlay/actions) → Latest workflow
- Download artifacts: `kbd_layout_overlay-macos` or `kbd_layout_overlay-windows`

**Or build from source:**

```bash
# Build
mkdir build && cd build
cmake ..
make                    # macOS
# or
cmake --build .         # Windows

# Run
./KbdLayoutOverlay.app  # macOS
./kbd_layout_overlay.exe # Windows
```

Hotkey: `Cmd+Option+Shift+/` (macOS) or `Ctrl+Alt+Shift+/` (Windows)

## Files

- `shared/` - Core logic, 98% of the code
- `macos/` - macOS implementation
- `windows/` - Windows implementation
- `assets/` - Put your keymap.png here

## Status

- ✅ Basic overlay working
- ✅ Cross-platform (macOS + Windows)
- ✅ Hotkey support
- ✅ System tray
- 🚧 Settings UI (basic)
- 🚧 Custom keymaps (partial)

## Notes

- macOS needs Input Monitoring permission
- Windows needs admin for global hotkeys
- Logs to `/tmp/kbd_layout_overlay.log`
- Built with CMake, no external deps
