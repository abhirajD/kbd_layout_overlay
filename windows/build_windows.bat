@echo off
setlocal
echo Building kbd_layout_overlay

:: Locate and load the Visual Studio build environment
for /f "usebackq tokens=*" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS_PATH=%%i
if not defined VS_PATH (
    echo Visual Studio Build Tools not found.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

rc /fo resource.res resource.rc
cl /O2 main.c ..\shared\overlay.c ..\shared\config.c resource.res user32.lib gdi32.lib /Fe:kbd_layout_overlay.exe /link /SUBSYSTEM:WINDOWS
endlocal

