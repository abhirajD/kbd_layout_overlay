# kbd_layout_overlay

> **Note:** The project is currently being rewritten using native APIs. The
> original Rust implementation has been moved to `legacy/` for historical
> reference and is no longer maintained.

Displays an image overlay in response to a keyboard shortcut.

## Usage

Running the binary launches a background listener. By default, holding
`Ctrl + Alt + Shift + Slash` shows the overlay and releasing any key hides
it. Enable persistent mode to toggle the overlay on and off with the same
shortcut. The image is centered horizontally and aligned to the bottom of the monitor
with the active window, falling back to the display under the mouse cursor. If no image is configured or the
configured path is missing, the application looks for a `keymap.png` next to
the executable and uses it if found. Otherwise a built-in `keymap.png`
(742×235) from the `shared/assets` directory is used. If a configured image cannot be
loaded an error is printed and the same `keymap.png` lookup is performed before
falling back to the built-in image.

Configuration options can be supplied on the command line:

```
kbd_layout_overlay --image path/to.png --width 742 --height 235 --opacity 0.3 --invert false --persist true --autostart true --hotkey ControlLeft+Alt+ShiftLeft+Slash
```

Use `--autostart true` to enable starting the application at login or
`--autostart false` to disable it. The preference is saved to the
configuration file.

Use `--hotkey` followed by a `+` separated list of key names to configure a
different shortcut.

Key names follow winit's `Key` enumeration. On macOS the Option key is
reported as `Alt` and the Command key as `MetaLeft`/`MetaRight`. The parser also
accepts `Option`/`Opt` in place of `Alt` and `Command`/`Cmd` in place of
`MetaLeft`.

Common combinations:

- **Windows:** `Ctrl+Alt+Shift+Slash` (default), `Ctrl+Alt+K`
- **macOS:** `Cmd+Option+Shift+/`, `Cmd+Option+K`
- **Linux:** `Ctrl+Alt+Shift+Slash`, `Meta+Alt+K` (Meta = Windows/Super key)

`kbd_layout_overlay diagnose` prints detected monitors and their scale
factors and exits.

Enable debug or informational output by setting the `RUST_LOG` environment
variable before running the application. For example:

```
RUST_LOG=debug kbd_layout_overlay
```

## Autostart helper

The CLI exposes commands to register the application to run at login on
Windows and macOS.

```
kbd_layout_overlay autostart enable   # register
kbd_layout_overlay autostart disable  # unregister
```

On Windows a registry entry is created under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. On macOS a
`LaunchAgents` plist named `com.kbd_layout_overlay.plist` is written to the
user's home directory and loaded with `launchctl`.

## Manual test checklist

- hotkey shows and hides overlay instantly
- overlay ignores mouse clicks
- image appears at the bottom center of the active monitor (cursor monitor fallback)
- size consistent on HiDPI displays
- autostart launches app after reboot
