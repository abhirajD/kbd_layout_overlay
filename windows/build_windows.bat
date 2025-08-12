@echo off
setlocal
echo Building kbd_layout_overlay
cl /O2 main.c ..\shared\overlay.c ..\shared\config.c user32.lib gdi32.lib /Fe:kbd_layout_overlay.exe /link /SUBSYSTEM:WINDOWS
copy ..\shared\assets\keymap.png keymap.png >nul

