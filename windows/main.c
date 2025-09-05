#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

/* Tray message constant */
#define WM_TRAY (WM_USER + 1)
#include "config.h"
#include "overlay.h"
#include "log.h"
#include "WindowManager.h"
#include "ImageManager.h"
#include "HotkeyManager.h"
#include "MenuController.h"
#include "prefs_win32.h"

static Config g_config;
static HWND g_hidden_window = NULL;

/* Control IDs */
#define ID_PREFS_OPEN 8



LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        /* Route all mouse events to underlying windows */
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        /* Prevent the overlay from ever taking focus */
        return MA_NOACTIVATE;

    case WM_TRAY:
        if (lParam == WM_RBUTTONUP) {
            menu_controller_show_menu();
        }
        break;

    case WM_TIMER:
        if (wParam == 1) {
            /* Auto-hide timer expired */
            window_manager_hide_overlay();
        } else if (wParam == 2) {
            /* Preview timer expired */
            KillTimer(g_hidden_window, 2);
            if (window_manager_is_visible()) {
                window_manager_hide_overlay();
            }
        }
        break;

    case WM_COMMAND:
        menu_controller_handle_command(LOWORD(wParam));
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow) {
    /* Initialize common controls for trackbar sliders */
    InitCommonControls();

    /* Per-monitor DPI awareness for crisp rendering */
    typedef BOOL (WINAPI *SetPDACTX)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        SetPDACTX setPDACtx = (SetPDACTX)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setPDACtx) {
            setPDACtx((DPI_AWARENESS_CONTEXT)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }

    g_config = get_default_config();
    load_config(&g_config, NULL);

    /* Initialize logger early for parity with macOS */
    logger_init();
    logger_log("KbdLayoutOverlay (Windows) starting up");

    // Initialize managers
    if (!image_manager_init(&g_config)) {
        logger_log("Failed to initialize image manager");
        logger_close();
        return 1;
    }

    if (!hotkey_manager_init(&g_config)) {
        logger_log("Failed to initialize hotkey manager");
        image_manager_cleanup();
        logger_close();
        return 1;
    }

    // Load overlay image
    if (!image_manager_load_overlay()) {
        logger_log("Failed to load overlay image");
        hotkey_manager_cleanup();
        image_manager_cleanup();
        logger_close();
        return 1;
    }

    /* Register window class */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "KbdLayoutOverlay";
    RegisterClassA(&wc);

    /* Create hidden message window */
    g_hidden_window = CreateWindowA("KbdLayoutOverlay", "", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, NULL, hInst, NULL);

    /* Initialize window manager */
    Overlay *overlay = image_manager_get_overlay();
    if (!window_manager_init(&g_config, overlay)) {
        logger_log("Failed to initialize window manager");
        hotkey_manager_cleanup();
        image_manager_cleanup();
        logger_close();
        return 1;
    }

    if (!window_manager_create_bitmap()) {
        logger_log("Failed to create bitmap");
        window_manager_cleanup();
        hotkey_manager_cleanup();
        image_manager_cleanup();
        logger_close();
        return 1;
    }

    /* Initialize menu controller */
    if (!menu_controller_init(&g_config, hInst, g_hidden_window)) {
        logger_log("Failed to initialize menu controller");
        window_manager_cleanup();
        hotkey_manager_cleanup();
        image_manager_cleanup();
        logger_close();
        return 1;
    }

    /* Setup callbacks */
    hotkey_manager_set_toggle_callback(window_manager_toggle_overlay);

    menu_controller_set_callbacks(
        window_manager_show_overlay,      // show
        window_manager_hide_overlay,      // hide
        window_manager_toggle_overlay,    // toggle
        image_manager_reload_if_needed    // config changed
    );

    /* Register hotkey */
    if (!hotkey_manager_register_hotkey()) {
        logger_log("Failed to register hotkey");
        menu_controller_cleanup();
        window_manager_cleanup();
        hotkey_manager_cleanup();
        image_manager_cleanup();
        logger_close();
        return 1;
    }

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* Cleanup */
    menu_controller_cleanup();
    window_manager_cleanup();
    hotkey_manager_cleanup();
    image_manager_cleanup();

    logger_log("KbdLayoutOverlay (Windows) exiting");
    logger_close();

    return 0;
}
