#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Temporary sources and executables
APPLY_C=$(mktemp "${TMPDIR:-/tmp}/bench_applyXXXX.c")
LOAD_C=$(mktemp "${TMPDIR:-/tmp}/bench_loadXXXX.c")
APPLY_EXE=$(mktemp "${TMPDIR:-/tmp}/bench_applyXXXX")
LOAD_EXE=$(mktemp "${TMPDIR:-/tmp}/bench_loadXXXX")
trap 'rm -f "$APPLY_C" "$LOAD_C" "$APPLY_EXE" "$LOAD_EXE"' EXIT

# Benchmark apply_opacity_inversion
cat > "$APPLY_C" <<'C_EOF'
#include "overlay.h"
#include <stdlib.h>

int main(void) {
    Overlay img;
    img.width = 1920;
    img.height = 1080;
    img.channels = 4;
    img.data = malloc((size_t)img.width * img.height * 4);
    if (!img.data) return 1;
    for (size_t i = 0; i < (size_t)img.width * img.height * 4; ++i) {
        img.data[i] = (unsigned char)i;
    }
    for (int i = 0; i < 100; ++i) {
        apply_opacity_inversion(&img, 0.5f, 1);
    }
    free(img.data);
    return 0;
}
C_EOF

# Benchmark load_overlay_image
cat > "$LOAD_C" <<'C_EOF'
#include "overlay.h"
int main(void) {
    for (int i = 0; i < 100; ++i) {
        Overlay img;
        if (load_overlay_image("shared/assets/keymap.png", 1920, 1080, &img) != 0) return 1;
        free_overlay(&img);
    }
    return 0;
}
C_EOF

cc -O2 -std=c99 "$APPLY_C" "$ROOT_DIR/shared/overlay.c" -I"$ROOT_DIR/shared" -lm -o "$APPLY_EXE"
cc -O2 -std=c99 "$LOAD_C" "$ROOT_DIR/shared/overlay.c" -I"$ROOT_DIR/shared" -lm -o "$LOAD_EXE"

OUT_FILE="$ROOT_DIR/benchmark_results.txt"
if command -v hyperfine >/dev/null 2>&1; then
    hyperfine "$APPLY_EXE" "$LOAD_EXE" | tee "$OUT_FILE"
else
    echo "hyperfine not found; using time instead" | tee "$OUT_FILE"
    time -p "$APPLY_EXE" 2>&1 | tee -a "$OUT_FILE"
    time -p "$LOAD_EXE" 2>&1 | tee -a "$OUT_FILE"
fi

echo "Benchmark results written to $OUT_FILE"
