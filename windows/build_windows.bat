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

:: Compile resources
rc /fo resource.res resource.rc
if errorlevel 1 (
    echo Resource compilation failed.
    exit /b 1
)

:: Compile each translation unit to object files (isolates header/include order issues)
cl /nologo /O2 /arch:SSE2 /c main.c
if errorlevel 1 (
    echo Compilation failed: main.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c ..\shared\overlay.c
if errorlevel 1 (
    echo Compilation failed: overlay.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c ..\shared\config.c
if errorlevel 1 (
    echo Compilation failed: config.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c ..\shared\hotkey.c
if errorlevel 1 (
    echo Compilation failed: hotkey.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c ..\shared\monitor.c
if errorlevel 1 (
    echo Compilation failed: monitor.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c ..\shared\error.c
if errorlevel 1 (
    echo Compilation failed: error.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c ..\shared\app_context.c
if errorlevel 1 (
    echo Compilation failed: app_context.c
    exit /b 1
)
cl /nologo /O2 /arch:SSE2 /c hotkey_win.c
if errorlevel 1 (
    echo Compilation failed: hotkey_win.c
    exit /b 1
)

cl /nologo /O2 /arch:SSE2 /c platform_win.c
if errorlevel 1 (
    echo Compilation failed: platform_win.c
    exit /b 1
)

:: Link object files and resource into final executable
cl /nologo main.obj overlay.obj config.obj hotkey.obj monitor.obj error.obj app_context.obj hotkey_win.obj platform_win.obj resource.res user32.lib gdi32.lib advapi32.lib shell32.lib /Fe:kbd_layout_overlay.exe /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo Link failed.
    exit /b 1
)

endlocal
