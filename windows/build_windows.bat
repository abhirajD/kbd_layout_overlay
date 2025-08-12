@echo off
setlocal
echo Building kbd_layout_overlay
rc /fo resource.res resource.rc
cl /O2 main.c ..\shared\overlay.c ..\shared\config.c resource.res user32.lib gdi32.lib /Fe:kbd_layout_overlay.exe /link /SUBSYSTEM:WINDOWS

