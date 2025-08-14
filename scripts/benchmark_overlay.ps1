$ErrorActionPreference = 'Stop'
$root = (Get-Item "$PSScriptRoot/..\").FullName
Set-Location $root

# Build the project
& "$root\windows\build_windows.bat"

# Create temporary benchmark program for apply_opacity_inversion
$benchC = Join-Path ([System.IO.Path]::GetTempPath()) "bench_overlay.c"
@'
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
'@ | Set-Content $benchC

$benchExe = Join-Path ([System.IO.Path]::GetTempPath()) "bench_overlay.exe"
& cl /nologo /O2 $benchC "$root\shared\overlay.c" /I "$root\shared" /Fe:$benchExe | Out-Null

$outFile = Join-Path $root "benchmark_results.txt"
if (Get-Command hyperfine -ErrorAction SilentlyContinue) {
    hyperfine $benchExe | Tee-Object -FilePath $outFile
} else {
    "hyperfine not found; using Measure-Command" | Tee-Object -FilePath $outFile
    (Measure-Command { & $benchExe } | Out-String) | Tee-Object -FilePath $outFile -Append
}
Write-Host "Benchmark results written to $outFile"
