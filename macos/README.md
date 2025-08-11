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
