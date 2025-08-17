#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$ROOT_DIR/KbdLayoutOverlay"
BUILD_DIR="$ROOT_DIR/build"
APP_NAME="Kbd Layout Overlay"
APP_DIR="$BUILD_DIR/$APP_NAME.app"

mkdir -p "$APP_DIR/Contents/MacOS"
mkdir -p "$APP_DIR/Contents/Resources"

cat > "$APP_DIR/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>Kbd Layout Overlay</string>
    <key>CFBundleIdentifier</key>
    <string>com.example.kbdlayoutoverlay</string>
    <key>CFBundleExecutable</key>
    <string>KbdLayoutOverlay</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
</dict>
</plist>
PLIST

# Convert PNG to C header for embedding
xxd -i "$ROOT_DIR/../shared/assets/keymap.png" > "$ROOT_DIR/keymap_data.h"
sed -i '' 's/unsigned char .*keymap_png\[\]/unsigned char keymap_png[]/' "$ROOT_DIR/keymap_data.h"
sed -i '' 's/unsigned int .*keymap_png_len/unsigned int keymap_png_len/' "$ROOT_DIR/keymap_data.h"

clang -fobjc-arc -O2 -I "$ROOT_DIR" \
      "$SRC_DIR/main.m" "$SRC_DIR/AppDelegate.m" "$SRC_DIR/OverlayView.m" \
      "$ROOT_DIR/../shared/config.c" "$ROOT_DIR/../shared/overlay.c" \
      "$ROOT_DIR/../shared/hotkey.c" "$ROOT_DIR/../shared/monitor.c" \
      -framework Cocoa -framework Carbon -framework ApplicationServices \
      -o "$APP_DIR/Contents/MacOS/KbdLayoutOverlay"

cp "$ROOT_DIR/../shared/assets/keymap.png" "$APP_DIR/Contents/Resources/"

echo "Built $APP_DIR"

if [[ "$1" == "--install" ]]; then
    PLIST_SRC="$ROOT_DIR/LaunchAgents/com.example.kbdlayoutoverlay.plist"
    DEST="$HOME/Library/LaunchAgents"
    mkdir -p "$DEST"
    cp "$PLIST_SRC" "$DEST/"
    launchctl unload "$DEST/com.example.kbdlayoutoverlay.plist" 2>/dev/null || true
    launchctl load "$DEST/com.example.kbdlayoutoverlay.plist"
    echo "LaunchAgent loaded"
fi
