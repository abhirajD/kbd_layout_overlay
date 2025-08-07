# kbd_layout_overlay

Displays an image overlay in response to a keyboard shortcut.

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
