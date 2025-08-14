# macOS Platform

This directory contains a minimal Cocoa application for the keyboard layout overlay.

## Building

Run `build_macos.sh` on macOS. It uses `clang` to create the `Kbd Layout Overlay.app` bundle:

```sh
./build_macos.sh
```

To install and load the LaunchAgent for autostart, pass `--install`:

```sh
./build_macos.sh --install
```

## Logging

For troubleshooting, run the built application with `--log-level debug` (alias `--logs`) or set `log="debug"` in the configuration file to increase verbosity.
Logs are written with timestamps to `kbd_overlay.log` in the current working directory.
To display the overlay when launching from the command line, include the `--run` flag; use `--help` for usage information.

## Signing and Notarization

With a Developer ID certificate the app can be signed and notarized before distribution:

```sh
# Sign
codesign --deep --force --options runtime --sign "Developer ID Application: Example (TEAMID)" build/Kbd\ Layout\ Overlay.app

# Notarize
cd build
zip -r kbd_layout_overlay-macos.zip "Kbd Layout Overlay.app"
xcrun notarytool submit kbd_layout_overlay-macos.zip --apple-id <apple-id> --team-id <team-id> --password <app-specific-password> --wait
xcrun stapler staple "Kbd Layout Overlay.app"
```

The `macos.yml` GitHub workflow performs these steps automatically when `MACOS_SIGNING_IDENTITY`, `APPLE_ID`, `TEAM_ID`, and `APPLE_PASSWORD` secrets are configured.

