#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Build the project
./macos/build_macos.sh

# Create temporary benchmark program for apply_opacity_inversion
BENCH_C=$(mktemp)
cat > "$BENCH_C" <<'C_EOF'
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
        img.data[i] = (unsigned char)(i);
    }
    for (int i = 0; i < 100; ++i) {
        apply_opacity_inversion(&img, 0.5f, 1);
    }
    free(img.data);
    return 0;
}
C_EOF

BENCH_EXE=$(mktemp)
cc -O2 -std=c99 "$BENCH_C" "$ROOT_DIR/shared/overlay.c" -I"$ROOT_DIR/shared" -o "$BENCH_EXE"

OUT_FILE="$ROOT_DIR/benchmark_results.txt"
if command -v hyperfine >/dev/null 2>&1; then
    hyperfine "$BENCH_EXE" | tee "$OUT_FILE"
else
    echo "hyperfine not found; using time instead" | tee "$OUT_FILE"
    /usr/bin/time -p "$BENCH_EXE" 2>&1 | tee -a "$OUT_FILE"
fi

echo "Benchmark results written to $OUT_FILE"
