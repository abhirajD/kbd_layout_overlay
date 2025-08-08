# kbd_layout_overlay

Displays an image overlay in response to a keyboard shortcut.

## Usage

Running the binary launches a background listener. Hold
`Ctrl + Alt + Shift + Slash` to show the overlay and release any key to hide
it. The image is centered on the monitor with the active window, falling back
to the display under the mouse cursor. If no image is configured a built-in
`keymap.png` (742×235) is used. If the configured image cannot be loaded the
hotkey is ignored and an error is printed.

Configuration options can be supplied on the command line:

```
kbd_layout_overlay --image path/to.png --width 742 --height 235 --opacity 0.3 --autostart true
```

Use `--autostart true` to enable starting the application at login or
`--autostart false` to disable it. The preference is saved to the
configuration file.

`kbd_layout_overlay diagnose` prints detected monitors and their scale
factors and exits.

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
- image is centered on the active monitor (cursor monitor fallback)
- size consistent on HiDPI displays
- autostart launches app after reboot
