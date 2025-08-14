# Windows Platform

Platform-specific sources and build scripts for Windows will reside here.

## Building

Run `build_windows.bat` in a Visual Studio Developer Command Prompt to produce `kbd_layout_overlay.exe`.

## Signing

When a code signing certificate is available the executable can be signed with `signtool`:

```cmd
signtool sign /f <path-to-cert.pfx> /p <password> kbd_layout_overlay.exe
```

## Logging

Use `--log-level debug` (alias `--logs`) or set `log="debug"` in the configuration file to enable verbose output when running the executable.

The `windows.yml` GitHub workflow will use `WINDOWS_CERT_FILE` and `WINDOWS_CERT_PASSWORD` secrets to sign automatically and will upload both the `.exe` and a zipped bundle.

