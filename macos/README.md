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

