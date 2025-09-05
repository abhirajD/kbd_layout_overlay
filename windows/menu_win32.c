#include "menu_win32.h"
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <stdio.h>
#include "../shared/config.h"
#include "../shared/log.h"

#define WM_TRAY_ICON (WM_USER + 1)
#define ID_TRAY_ICON 1

struct TrayMenu {
    NOTIFYICONDATA nid;
    HMENU hMenu;
    WNDPROC originalWndProc;
    Config config;
    void (*toggle_callback)(void);
    void (*quit_callback)(void);
};

static TrayMenu *g_current_tray_menu = NULL;

static LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAY_ICON && g_current_tray_menu) {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            SetForegroundWindow(hwnd);
            TrackPopupMenu(g_current_tray_menu->hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                          pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
    }

    if (g_current_tray_menu && g_current_tray_menu->originalWndProc) {
        return CallWindowProc(g_current_tray_menu->originalWndProc, hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

TrayMenu* create_tray_menu(HWND hwnd, HINSTANCE hInstance, Config config,
                          void (*toggle_callback)(void), void (*quit_callback)(void)) {
    if (!hwnd || !toggle_callback || !quit_callback) return NULL;

    TrayMenu *menu = calloc(1, sizeof(TrayMenu));
    if (!menu) return NULL;

    menu->config = config;
    menu->toggle_callback = toggle_callback;
    menu->quit_callback = quit_callback;

    // Create popup menu
    menu->hMenu = CreatePopupMenu();
    if (!menu->hMenu) {
        free(menu);
        return NULL;
    }

    // Add menu items
    AppendMenu(menu->hMenu, MF_STRING, 1, "Show Keymap");
    AppendMenu(menu->hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(menu->hMenu, MF_STRING, 2, "Scale 75%");
    AppendMenu(menu->hMenu, MF_STRING, 3, "Scale 100%");
    AppendMenu(menu->hMenu, MF_STRING, 4, "Scale 125%");
    AppendMenu(menu->hMenu, MF_STRING, 5, "Scale 150%");
    AppendMenu(menu->hMenu, MF_STRING, 6, "Fit Screen");
    AppendMenu(menu->hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(menu->hMenu, MF_STRING, 7, "Opacity 50%");
    AppendMenu(menu->hMenu, MF_STRING, 8, "Opacity 70%");
    AppendMenu(menu->hMenu, MF_STRING, 9, "Opacity 85%");
    AppendMenu(menu->hMenu, MF_STRING, 10, "Opacity 100%");
    AppendMenu(menu->hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(menu->hMenu, MF_STRING, 11, "Auto-hide Off");
    AppendMenu(menu->hMenu, MF_STRING, 12, "Auto-hide 0.8s");
    AppendMenu(menu->hMenu, MF_STRING, 13, "Auto-hide 2.0s");
    AppendMenu(menu->hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(menu->hMenu, MF_STRING, 14, "Preview Keymap");
    AppendMenu(menu->hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(menu->hMenu, MF_STRING, 15, "Preferences...");
    AppendMenu(menu->hMenu, MF_STRING, 16, "Quit");

    // Setup tray icon
    menu->nid.cbSize = sizeof(NOTIFYICONDATA);
    menu->nid.hWnd = hwnd;
    menu->nid.uID = ID_TRAY_ICON;
    menu->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    menu->nid.uCallbackMessage = WM_TRAY_ICON;
    menu->nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(menu->nid.szTip, "KbdLayoutOverlay");

    if (!Shell_NotifyIcon(NIM_ADD, &menu->nid)) {
        DestroyMenu(menu->hMenu);
        free(menu);
        return NULL;
    }

    // Subclass window to handle tray messages
    menu->originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)TrayWindowProc);

    g_current_tray_menu = menu;
    logger_log("Windows tray menu created");

    return menu;
}

void destroy_tray_menu(TrayMenu* menu) {
    if (!menu) return;

    if (menu->nid.hWnd && menu->originalWndProc) {
        SetWindowLongPtr(menu->nid.hWnd, GWLP_WNDPROC, (LONG_PTR)menu->originalWndProc);
    }

    Shell_NotifyIcon(NIM_DELETE, &menu->nid);

    if (menu->hMenu) {
        DestroyMenu(menu->hMenu);
    }

    if (g_current_tray_menu == menu) {
        g_current_tray_menu = NULL;
    }

    free(menu);
    logger_log("Windows tray menu destroyed");
}

void update_tray_menu(TrayMenu* menu, Config new_config) {
    if (!menu) return;
    menu->config = new_config;
}

void show_tray_menu(TrayMenu* menu, HWND hwnd) {
    if (!menu || !hwnd) return;

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu->hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                  pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
}

void set_tray_icon_tooltip(TrayMenu* menu, const char* tooltip) {
    if (!menu || !tooltip) return;

    strncpy(menu->nid.szTip, tooltip, sizeof(menu->nid.szTip) - 1);
    menu->nid.szTip[sizeof(menu->nid.szTip) - 1] = '\0';

    Shell_NotifyIcon(NIM_MODIFY, &menu->nid);
}
